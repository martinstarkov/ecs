/*

MIT License

Copyright (c) 2021 | Martin Starkov | https://github.com/martinstarkov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <cstdlib> // std::size_t, std::malloc, std::realloc, std::free
#include <cstdint> // std::uint32_t
#include <vector> // std::vector
#include <array> // std::array
#include <deque> // std::deque
#include <tuple> // std::tuple
#include <functional> // std::hash
#include <utility> // std::exchange
#include <algorithm> // std::max_element
#include <memory> // std::allocator
#include <type_traits> // std::enable_if_t, std::is_destructible_v, std::is_base_of_v, etc
#include <cassert> // assert

namespace ecs {

// Forward declarations for user-accessible types.

class Entity;
class NullEntity;
template <typename ...TComponents>
class System;
class Manager;

namespace internal {

// Forward declarations for internally used types.

class BasePool;
class BaseSystem;

// Aliases.

using Id = std::uint32_t;
using Version = std::uint32_t;
// Type representing the offset of a component 
// from the start of a memory block in a pool.
using Offset = std::uint32_t;

// Constants.

// Represents an invalid version number.
inline constexpr Version null_version{ 0 };

namespace type_traits {

template <typename TComponent>
inline constexpr bool is_valid_component_v{ 
	   std::is_destructible_v<TComponent>
	&& std::is_move_constructible_v<TComponent>
};

template <typename TComponent>
using is_valid_component = std::enable_if_t<
	is_valid_component_v<TComponent>,
	bool>;

template <typename T, typename ...TArgs>
using is_constructible = std::enable_if_t<std::is_constructible_v<T, TArgs...>, bool>;

// Enforced inheritence from:
// https://stackoverflow.com/a/22592618
template <template <typename> typename T, typename U>
struct IsDerivedFrom {
private:
	template <typename ...V>
	static decltype(static_cast<const T<V...>&>(std::declval<U>()), std::true_type{})
		test(const T<V...>&);

	static std::false_type test(...);
public:
	static constexpr bool value{ decltype(IsDerivedFrom::test(std::declval<U>()))::value };
};

template <typename TSystem>
inline constexpr bool is_system_v{ IsDerivedFrom<System, TSystem>::value };

template <typename TSystem>
using is_valid_system = std::enable_if_t<is_system_v<TSystem>, bool>;

} // namespace type_traits

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual BasePool* Clone() const = 0;
	virtual void Clear() = 0;
	virtual bool Remove(const Id entity) = 0;
	virtual std::size_t Hash() const = 0;
};

class BaseSystem {
public:
	virtual void Update() = 0;
	virtual ~BaseSystem() = default;
private:
	friend class Manager;
	virtual void SetManager(Manager* manager) = 0;
	virtual void SetComponentDependencies() = 0;
	virtual void FlagIfDependsOnNone() = 0;
	virtual BaseSystem* Clone() const = 0;
	virtual void ResetCacheIfFlagged() = 0;
	virtual void FlagIfDependsOn(const internal::Id component) = 0;
	virtual void FlagForReset() = 0;
	virtual std::size_t Hash() const = 0;
};

/* 
* Container which stores the offsets of components
* from the beginning of a memory block pointer.
* Offsets can be accessed using unique entity ids.
* Component must be move constructible and destructible.
* @tparam TComponent Type of component to store in the pool.
*/
template <typename TComponent, 
	type_traits::is_valid_component<TComponent> = true>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		// This enables capacity to double in powers of 2.
		AllocateMemoryBlock(1);
	}

	~Pool() {
		CallComponentDestructors();
		FreeMemoryBlock();
		assert(block_ == nullptr && "Pool memory must be freed before pool destruction");
	}

	// Component pools should never be copied.
	// Use Clone() instead.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	// Move operator used when resizing a vector 
	// of component pools (in the manager class).
	Pool(Pool&& obj) noexcept :
		block_{ obj.block_ },
		capacity_{ obj.capacity_ },
		size_{ obj.size_ },
		offsets_{ std::exchange(obj.offsets_, {}) },
		freed_offsets_{ std::exchange(obj.freed_offsets_, {}) } {
		obj.block_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	
	// Pools should never be move assigned.
	Pool& operator=(Pool&&) = delete;
	
	/*
	* Creates a duplicate (copy) pool with
	* all the same components and offsets.
	* @return Pointer to an identical component pool.
	*/
	virtual BasePool* Clone() const override final {
		static_assert(std::is_copy_constructible_v<TComponent>, 
					  "Cannot clone pool with a non copy constructible component");
		// Empty memory block for clone is allocated in constructor.
		auto clone = new Pool<TComponent>(
			capacity_, 
			size_, 
			offsets_, 
			freed_offsets_
		);
		// Copy entire pool block over to new pool block.
		for (auto offset : offsets_) {
			if (offset != 0) {
				TComponent* component = block_ + (offset - 1);
				TComponent* address = clone->block_ + (offset - 1);
				// Copy component from current pool to 
				// other pool using copy constructor.
				new(address) TComponent{ *component };
			}
		}
		return clone;
	}

	// Calls destructor on each component.
	// Clears free and occupied offsets.
	// Does not modify pool capacity.
	// Equivalent to clearing a vector.
	virtual void Clear() override final {
		CallComponentDestructors();
		offsets_.clear();
		freed_offsets_.clear();
		size_ = 0;
	}

	/*
	* Removes the component from the given entity.
	* @param entity Id of the entity to remove a component from.
	* @return True if component was removed, false otherwise.
	*/
	virtual bool Remove(const Id entity) override final {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != 0) {
				TComponent* address = block_ + (offset - 1);
				// Call destructor on component memory location.
				address->~TComponent();
				freed_offsets_.emplace_back(offset);
				// Set offset to invalid.
				offset = 0;
				return true;
			}
		}
		return false;
	}

	/*
	* Creates / replaces a component for an entity in the pool.
	* Component must be constructible from the given arguments.
	* @tparam TArgs Types of constructor arguments.
	* @param entity Id of the entity to add a component to.
	* @param constructor_args Arguments to be passed to the component constructor.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename ...TArgs, 
		type_traits::is_constructible<TComponent, TArgs...> = true>
	TComponent& Add(const Id entity, TArgs&&... constructor_args) {
		auto offset = GetAvailableOffset();
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		if (static_cast<std::size_t>(entity) >= offsets_.size()) {
			offsets_.resize(static_cast<std::size_t>(entity) + 1, 0);
		}
		// Check if component offset exists already.
		bool replace = offsets_[entity] != 0;
		offsets_[entity] = offset;
		TComponent* address = block_ + (offset - 1);
		if (replace) {
			// Call destructor on potential previous components
			// at the address.
			address->~TComponent();
		}
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		assert(address != nullptr && "Failed to create component at offset memory location");
		return *address;
	}

	/*
	* Checks if the entity has a component in the pool.
	* @param entity Id of the entity to check a component for.
	* @return True if the pool contains a valid component offset, false otherwise.
	*/
	bool Has(const Id entity) const {
		return entity < offsets_.size() && offsets_[entity] != 0;
	}

	/*
	* Retrieves the component of a given entity from the pool.
	* @param entity Id of the entity to retrieve a component for.
	* @return Const pointer to the component, nullptr if the component does not exist.
	*/
	const TComponent* Get(const Id entity) const {
		if (Has(entity)) {
			return block_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}

	/*
	* Retrieves the component of a given entity from the pool.
	* @param entity Id of the entity to retrieve a component for.
	* @return Pointer to the component, nullptr if the component does not exist.
	*/
	TComponent* Get(const Id entity) {
		if (Has(entity)) {
			return block_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}

	/*
	* Generates a hash number using pool members.
	* Useful for identifying if two pools are identical.
	* @return Hash code for the pool.
	*/
	virtual std::size_t Hash() const override final {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<Offset>()(size_);
		h = h * 31 + std::hash<Offset>()(capacity_);
		// Modified container hashing from:
		// https://stackoverflow.com/a/27216842
		auto container_hash = [](auto& v) {
			std::size_t seed = v.size();
			for (auto i : v) {
				seed ^= static_cast<std::size_t>(i) 
					+ 0x9e3779b9 
					+ (seed << 6) 
					+ (seed >> 2);
			}
			return seed;
		};
		h = h * 31 + container_hash(offsets_);
		h = h * 31 + container_hash(freed_offsets_);
		return h;
	}

private:
	// Constructor used for cloning identical pools.
	Pool(const Offset capacity,
		 const Offset size,
		 const std::vector<Offset>& offsets,
		 const std::deque<Offset>& freed_offsets) : 
		// Allocate memory block before capacity is set 
		// as otherwise capacity == 0 assertion fails.
		capacity_{ (AllocateMemoryBlock(capacity), capacity) },
		size_{ size },
		offsets_{ offsets },
		freed_offsets_{ freed_offsets } {
	}

	/*
	* Allocate an initial memory block for the pool.
	* @param starting_capacity The starting capacity of the pool.
	* (number of components it should support to begin with).
	*/
	void AllocateMemoryBlock(const Offset starting_capacity) {
		assert(block_ == nullptr);
		assert(capacity_ == 0);
		assert(size_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		block_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		assert(block_ != nullptr && "Could not properly allocate memory for component pool");
	}

	// Invokes the destructor of each valid component in the pool.
	// Note: valid offsets are not refreshed afterward.
	void CallComponentDestructors() {
		for (auto offset : offsets_) {
			// Only consider valid offsets.
			if (offset != 0) {
				TComponent* address = block_ + (offset - 1);
				address->~TComponent();
			}
		}
	}

	// Frees the allocated memory block associated with the pool.
	// This must be called whenever destroying a pool.
	void FreeMemoryBlock() {
		assert(block_ != nullptr && "Cannot free invalid component pool pointer");
		std::free(block_);
		block_ = nullptr;
	}

	/*
	* Doubles the capacity of a pool if the current capacity is exceeded.
	* @param new_size Desired size of the pool.
	* (minimum number of components it should support).
	*/
	void ReallocateIfNeeded(const Offset new_size) {
		if (new_size >= capacity_) {
			// Double the capacity.
			capacity_ = new_size * 2;
			assert(block_ != nullptr && "Pool memory must be allocated before reallocation");
			block_ = static_cast<TComponent*>(std::realloc(block_, capacity_ * sizeof(TComponent)));
		}
	}
	
	/*
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset is available.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset next_offset{ 0 };
		if (freed_offsets_.size() > 0) {
			// Take offset from the front of the free offsets.
			// This better preserves component locality as
			// components are pooled (pun) in the front.
			next_offset = freed_offsets_.front();
			freed_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			// Offset of 0 is considered invalid.
			// In each access case 1 is subtracted so 0th
			// offset is still used.
			next_offset = ++size_;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_offset != 0 && "Could not find a valid offset from component pool");
		return next_offset;
	}

	// Pointer to the beginning of the pool's memory block.
	TComponent* block_{ nullptr };

	// Component capacity of the pool.
	Offset capacity_{ 0 };

	// Number of components currently in the pool.
	Offset size_{ 0 };

	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the block_ pointer.
	std::vector<Offset> offsets_;

	// Queue of free offsets (avoids frequent reallocation of pools).
	// Double ended queue is chosen as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> freed_offsets_;
};

} // namespace internal

template <typename ...TRequiredComponents>
class System : public internal::BaseSystem {
public:
	System() = default;
	virtual ~System() = default;

	// Override this function in order to call
	// manager's UpdateSystem() function.
	virtual void Update() override {}

	Manager& GetManager() {
		assert(manager_ != nullptr && "Cannot retrieve manager for uninitialized system");
		return *manager_;
	}
protected:
	std::vector<std::tuple<Entity, TRequiredComponents&...>> entities;
private:
	friend class Manager;

	virtual std::size_t Hash() const override final {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<Manager*>()(manager_);
		h = h * 31 + std::hash<bool>()(reset_required_);
		h = h * 31 + std::hash<std::vector<bool>>()(components_);
		return h;
	}

	System(Manager* manager,
		   const std::vector<bool>& components,
		   bool reset_required) :
		manager_{ manager },
		components_{ components },
		reset_required_{ reset_required } {
	}

	virtual void SetManager(Manager* manager) override final {
		assert(manager != nullptr && "Cannot set system manager to nullptr");
		manager_ = manager;
	}

	virtual void FlagForReset() override final {
		reset_required_ = true;
	}

	virtual void FlagIfDependsOn(const internal::Id component) override final {
		if (component < components_.size() && components_[component]) {
			reset_required_ = true;
		}
	}

	virtual internal::BaseSystem* Clone() const override final {
		return new System<TRequiredComponents...>(manager_, components_, reset_required_);
	}

	virtual void SetComponentDependencies() override final;

	virtual void ResetCacheIfFlagged() override final;

	virtual void FlagIfDependsOnNone() override final {
		if constexpr (sizeof...(TRequiredComponents) == 0) {
			reset_required_ = true;
		}
	}

	Manager* manager_{ nullptr };
	std::vector<bool> components_;
	bool reset_required_{ false };
};

class Manager {
public:
	Manager() {
		// Reserve capacity for 1 entity so that
		// manager size will double in powers of 2.
		Reserve(1);
	}

	~Manager() {
		DestroySystems();
		DestroyPools();
	}

	// Managers cannot be copied. Use Clone() if you wish 
	// to create a new manager with identical composition.
	Manager& operator=(const Manager&) = delete;
	Manager(const Manager&) = delete;

	Manager(Manager&& obj) noexcept :
		next_entity_{ obj.next_entity_ },
		entities_{ std::exchange(obj.entities_, {}) },
		refresh_{ std::exchange(obj.refresh_, {}) },
		versions_{ std::exchange(obj.versions_, {}) },
		pools_{ std::exchange(obj.pools_, {}) },
		systems_{ std::exchange(obj.systems_, {}) },
		free_entities_{ std::exchange(obj.free_entities_, {}) } {
		obj.next_entity_ = 0;
	}

	Manager& operator=(Manager&& obj) noexcept {
		// Destroy previous manager internals.
		DestroySystems();
		DestroyPools();
		// Move manager into current manager.
		next_entity_ = obj.next_entity_;
		entities_ = std::exchange(obj.entities_, {});
		refresh_ = std::exchange(obj.refresh_, {});
		versions_ = std::exchange(obj.versions_, {});
		pools_ = std::exchange(obj.pools_, {});
		systems_ = std::exchange(obj.systems_, {});
		free_entities_ = std::exchange(obj.free_entities_, {});
		// Reset state of other manager.
		obj.next_entity_ = 0;
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use comparison in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition is identical, false otherwise.
	*/
	bool operator==(const Manager& other) const {
		return next_entity_ == other.next_entity_
			&& entities_ == other.entities_
			&& versions_ == other.versions_
			&& free_entities_ == other.free_entities_
			&& refresh_ == other.refresh_
			// Compare manager systems.
			&& std::equal(std::begin(systems_), std::end(systems_),
						  std::begin(other.systems_), std::end(other.systems_),
						  [](const internal::BaseSystem* lhs, const internal::BaseSystem* rhs) {
				return lhs && rhs && lhs->Hash() == rhs->Hash();
			})
			// Compare manager component pools.
			&& std::equal(std::begin(pools_), std::end(pools_),
						  std::begin(other.pools_), std::end(other.pools_),
						  [](const internal::BasePool* lhs, const internal::BasePool* rhs) {
				return lhs && rhs && lhs->Hash() == rhs->Hash();
			});
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use comparison in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition differs in any way, false otherwise.
	*/
	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}

	/*
	* Copying managers accidentally is expensive.
	* This provides a way of replicating a manager with
	* identical entities and components.
	* @return New manager with identical composition.
	*/
	Manager Clone() const {
		Manager clone;
		clone.next_entity_ = next_entity_;
		clone.entities_ = entities_;
		clone.refresh_ = refresh_;
		clone.versions_ = versions_;
		clone.free_entities_ = free_entities_;
		clone.pools_.resize(pools_.size(), nullptr);
		for (std::size_t i = 0; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool != nullptr) {
				// Clone pools over to new manager.
				clone.pools_[i] = pool->Clone();
			}
		}
		clone.systems_.resize(systems_.size(), nullptr);
		for (std::size_t i = 0; i < systems_.size(); ++i) {
			auto system = systems_[i];
			if (system != nullptr) {
				// Clone systems over to new manager.
				clone.systems_[i] = system->Clone();
			}
		}
		assert(clone == *this && "Cloning manager failed");
		return clone;
	}

	// Clears entity cache and reset component pools to empty ones.
	// Keeps entity capacity unchanged.
	// Systems are not removed but caches are flagged for reset.
	void Clear() {
		next_entity_ = 0;

		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();

		for (auto pool : pools_) {
			if (pool != nullptr) {
				pool->Clear();
			}
		}
		for (auto system : systems_) {
			if (system != nullptr) {
				system->FlagForReset();
			}
		}
	}

	// Cycles through all entities and destroys 
	// ones that have been marked for destruction.
	void Refresh() {
		assert(entities_.size() == versions_.size());
		assert(entities_.size() == refresh_.size());
		assert(next_entity_ <= entities_.size());
		internal::Id alive{ 0 };
		internal::Id dead{ 0 };
		for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
			// Entity was marked for refresh.
			if (refresh_[entity]) {
				refresh_[entity] = false;
				if (entities_[entity]) { // Marked for deletion.
					RemoveComponents(entity);
					entities_[entity] = false;
					++versions_[entity];
					free_entities_.emplace_back(entity);
					++dead;
				} else { // Marked for 'creation'.
					entities_[entity] = true;
					++alive;
				}
			}
		}
		if (alive > 0) {
			// If even a single entity was created, flag refresh for
			// all systems that don't depend on specific components.
			for (auto& system : systems_) {
				if (system) {
					system->FlagIfDependsOnNone();
				}
			}
		}
		assert(alive >= 0 && dead >= 0);
		// Update entity count with net change.
		count_ += alive - dead;
		assert(count_ >= 0);
	}

	// Equivalent to creating an entirely new manager.
	// Clears entity caches and destroys component pools.
	// Resets all capacities to 0.
	void Reset() {
		next_entity_ = 0;

		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();

		entities_.shrink_to_fit();
		refresh_.shrink_to_fit();
		versions_.shrink_to_fit();
		free_entities_.shrink_to_fit();

		DestroyPools();
		pools_.clear();
		pools_.shrink_to_fit();

		DestroySystems();
		systems_.clear();
		systems_.shrink_to_fit();

		Reserve(1);
	}

	/*
	* @return A handle to a new entity object.
	*/
	Entity CreateEntity();

	/*
	* @return A vector of handles to each living entity in the manager.
	*/
	std::vector<Entity> GetEntities();

	template <typename ...TComponents>
	std::vector<Entity> GetEntitiesWith();

	template <typename ...TComponents>
	std::vector<Entity> GetEntitiesWithout();

	void DestroyEntities();

	template <typename ...TComponents>
	void DestroyEntitiesWith();

	template <typename ...TComponents>
	void DestroyEntitiesWithout();

	/*
	* @return The number of entities which are currently alive in the manager.
	*/
	std::size_t GetEntityCount() const {
		return count_;
	}

	/*
	* @return The number of entities which are currently not alive in the manager.
	*/
	std::size_t GetDeadEntityCount() const {
		return free_entities_.size();
	}

	/*
	* Reserve additional memory for entities.
	* @param capacity Desired capacity of the manager.
	* If smaller than current capacity, nothing happens.
	*/
	void Reserve(const std::size_t capacity) {
		entities_.reserve(capacity);
		refresh_.reserve(capacity);
		versions_.reserve(capacity);
		assert(entities_.capacity() == refresh_.capacity());
		// TODO: Figure out how to test this.
		//assert(versions_.capacity() == entities_.capacity());
	}

	template <typename ...TComponents>
	std::vector<std::tuple<Entity, TComponents&...>> GetComponentTuple();

	template <typename TSystem>
	void AddSystemImpl() {
		auto system_id = GetSystemId<TSystem>();
		if (system_id >= systems_.size()) {
			systems_.resize(static_cast<std::size_t>(system_id) + 1, nullptr);
		}
		// Destroy system if it already exists in the manager.
		delete GetSystem<TSystem>(system_id);
		auto system = new TSystem();
		assert(system != nullptr && "Could not create new system correctly");
		system->TSystem::SetComponentDependencies();
		system->TSystem::SetManager(this);
		systems_[system_id] = system;
	}

	template <typename TSystem>
	void AddSystem() {
		static_assert(internal::type_traits::is_system_v<TSystem>,
					  "Cannot add a system to the manager which does not inherit from ecs::System class");
		AddSystemImpl<TSystem>();
	}

	template <typename TSystem>
	void UpdateSystemImpl() {
		auto system_id = GetSystemId<TSystem>();
		assert(HasSystem<TSystem>() && "Cannot update a system which does not exist in manager");
		auto system = GetSystem<TSystem>(system_id);
		system->TSystem::ResetCacheIfFlagged();
		system->TSystem::Update();
	}

	template <typename TSystem>
	void UpdateSystem() {
		static_assert(internal::type_traits::is_system_v<TSystem>,
					  "Cannot update a system which does not inherit from ecs::System class");
		UpdateSystemImpl<TSystem>();
	}

	template <typename TSystem>
	bool HasSystem() const {
		if constexpr (!internal::type_traits::is_system_v<TSystem>) {
			return false;
		} else {
			auto system = GetSystemId<TSystem>();
			return system < systems_.size() && systems_[system] != nullptr;
		}
	}

	template <typename TSystem>
	void RemoveSystem() {
		if constexpr (internal::type_traits::is_system_v<TSystem>) {
			auto system = GetSystemId<TSystem>();
			if (system < systems_.size()) {
				delete systems_[system];
			}
		}
	}
private:
	// Destroy and deallocate all the component pools.
	void DestroyPools() {
		for (auto pool : pools_) {
			delete pool;
		}
	}

	void DestroySystems() {
		for (auto system : systems_) {
			delete system;
		}
	}

	/*
	* Resize vector of entities, refresh marks and versions.
	* If smaller than current size, nothing happens.
	* @param size Desired size of the vectors.
	*/
	void Resize(const std::size_t size) {
		if (size > entities_.size()) {
			entities_.resize(size, false);
			refresh_.resize(size, false);
			versions_.resize(size, internal::null_version);
		}
		assert(entities_.size() == refresh_.size());
		assert(entities_.size() == versions_.size());
	}

	/*
	* Marks entity for deletion during next manager refresh.
	* @param entity Id of entity to mark for deletion.
	* @param version Version of entity for handle comparison.
	*/
	void DestroyEntity(const internal::Id entity, const internal::Version version) {
		assert(entity < versions_.size());
		assert(entity < refresh_.size());
		if (versions_[entity] == version) {
			refresh_[entity] = true;
		}
	}

	/*
	* Checks if entity is valid and alive.
	* @param entity Id of entity to check.
	* @param version Version of entity for handle comparison.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive(const internal::Id entity, const internal::Version version) const {
		return entity < versions_.size()
			&& versions_[entity] == version
			&& entity < entities_.size()
			&& (entities_[entity] || refresh_[entity]);
	}

	/*
	* Retrieve a const pointer to the component pool with matching component id.
	* @tparam TComponent Type of component to retrieve pool for.
	* @param component Id of component to retrieve pool for.
	* @return Const pointer to the component pool, nullptr if pool does not exist.
	*/
	template <typename TComponent>
	internal::Pool<TComponent>* GetPool(const internal::Id component) const {
		assert(component == GetComponentId<TComponent>());
		if (component < pools_.size()) {
			return static_cast<internal::Pool<TComponent>*>(pools_[component]);
		}
		return nullptr;
	}

	/*
	* Retrieve a pointer to the system with matching system id.
	* @tparam TSystem Type of system to retrieve.
	* @param system Id of system to retrieve.
	* @return Pointer to the system, nullptr if system does not exist.
	*/
	template <typename TSystem>
	TSystem* GetSystem(const internal::Id system) {
		assert(system == GetSystemId<TSystem>());
		if (system < systems_.size()) {
			return static_cast<TSystem*>(systems_[system]);
		}
		return nullptr;
	}

	void ComponentChange(const internal::Id entity, const internal::Id component) {
		assert(entity < entities_.size());
		// Note that even a "not currently alive" can trigger cache refresh.
		for (auto& system : systems_) {
			if (system) {
				system->FlagIfDependsOn(component);
			}
		}
	}

	/*
	* Retrieves a const reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component to retrieve.
	* @param entity Id of entity to retrieve component for.
	* @param component Id of component to retrieve.
	* @return Const reference to the component.
	*/
	template <typename TComponent>
	const TComponent& GetComponent(const internal::Id entity, const internal::Id component) const {
		const auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr && "Cannot retrieve component which has not been added to manager");
		const auto component_address = pool->Get(entity);
		// Debug tip: 
		// If you ended up here and want to find out which
		// entity called this function, follow the call stack.
		assert(component_address != nullptr && "Cannot get component which entity does not have");
		return *component_address;
	}

	/*
	* Retrieves a reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component to retrieve.
	* @param entity Id of entity to retrieve component for.
	* @param component Id of component to retrieve.
	* @return Reference to the component.
	*/
	template <typename TComponent>
	TComponent& GetComponent(const internal::Id entity, const internal::Id component) {
		const auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr && "Cannot retrieve component which has not been added to manager");
		auto component_address = pool->Get(entity);
		// Debug tip: 
		// If you ended up here and want to find out which
		// entity called this function, follow the call stack.
		assert(component_address != nullptr && "Cannot get component which entity does not have");
		return *component_address;
	}

	/*
	* Returns whether or not an entity has the component type.
	* @tparam TComponent Type of component to check.
	* @param entity Id of entity to check component for.
	* @param component Id of component to check.
	* @return True if entity has component, false otherwise.
	*/
	template <typename TComponent>
	bool HasComponent(const internal::Id entity, const internal::Id component) const {
		const auto pool = GetPool<TComponent>(component);
		return pool != nullptr && pool->Has(entity);
	}

	/*
	* Returns whether or not an entity has all the given component types.
	* @tparam TComponents Types of components to check.
	* @param entity Id of entity to check components for.
	* @return True if entity has each component type, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents(const internal::Id entity) const {
		return { (HasComponent<TComponents>(entity, GetComponentId<TComponents>()) && ...) };
	}

	/*
	* Adds a component to the specified manager entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param entity Id of entity to add component to.
	* @param component Id of component to add.
	* @param constructor_args Arguments for constructing component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(const internal::Id entity, const internal::Id component, TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>, "Cannot construct component type from given arguments");
		static_assert(std::is_destructible_v<TComponent>, "Cannot add component which does not have a valid destructor");
		if (component >= pools_.size()) {
			pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
		}
		auto pool = GetPool<TComponent>(component);
		bool new_pool = pool == nullptr;
		// If component type has not been added to manager,
		// generate a new pool for the given type.
		if (new_pool) {
			pool = new internal::Pool<TComponent>();
			pools_[component] = pool;
		}
		assert(pool != nullptr && "Could not create new component pool correctly");
		bool new_component = !pool->Has(entity);
		auto& component_reference = pool->Add(entity, std::forward<TArgs>(constructor_args)...);
		if (new_pool || new_component) {
			ComponentChange(entity, component);
		}
		return component_reference;
	}

	/*
	* Removes a component from the specified manager entity.
	* If entity does not have component type, nothing happens.
	* @tparam TComponent Type of component to remove.
	* @param entity Id of entity to remove component from.
	* @param component Id of component to remove.
	*/
	template <typename TComponent>
	void RemoveComponent(const internal::Id entity, const internal::Id component) {
		auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr && "Cannot remove component which has never been added to a manager entity");
		// Static call to derived component pool class (no dynamic dispatch).
		bool removed = pool->internal::Pool<TComponent>::Remove(entity);
		if (removed) {
			ComponentChange(entity, component);
		}
	}

	template <typename ...TComponents>
	void RemoveComponents(const internal::Id entity) {
		(RemoveComponent<TComponents>(entity, GetComponentId<TComponents>()), ...);
	}

	/*
	* Destroy all components associated with an entity.
	* Results in virtual function call on each component pool.
	* @param entity Id of entity to remove components from.
	*/
	void RemoveComponents(const internal::Id entity) {
		for (internal::Id i{ 0 }; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool) {
				bool removed = pool->Remove(entity);
				if (removed) {
					ComponentChange(entity, i);
				}
			}
		}
	}

	/*
	* Return a unique id for a type of component.
	* @tparam TComponent Type of component.
	* @return Unique id for the given component type.
	*/
	template <typename TComponent>
	static internal::Id GetComponentId() {
		// Get the next available id save that id as
		// a static variable for the component type.
		static internal::Id id{ ComponentCount()++ };
		return id;
	}

	/*
	* Important design decision: Component ids shared among all created managers
	* I.e. struct Position has id '3' in all manager instances, as opposed to
	* the order in which a component is first added to each manager.
	* @return Next available component id.
	*/
	static internal::Id& ComponentCount() {
		static internal::Id id{ 0 };
		return id;
	}

	/*
	* Return a unique id for a type of system.
	* @tparam TSystem Type of system.
	* @return Unique id for the given system type.
	*/
	template <typename TSystem>
	static internal::Id GetSystemId() {
		// Get the next available id save that id as
		// a static variable for the system type.
		static internal::Id id{ SystemCount()++ };
		return id;
	}

	/*
	* Important design decision: System ids shared among all created managers
	* I.e. class HealthSystem has id '3' in all manager instances, as opposed to
	* the order in which a system is first added to each manager.
	* @return Next available system id.
	*/
	static internal::Id& SystemCount() {
		static internal::Id id{ 0 };
		return id;
	}

	// Entity handles must have access to internal functions.
	// This because ids are internal and (mostly) hidden from the user.
	friend class Entity;

	// Systems require access to entity component access functions.
	template <typename ...T>
	friend class System;

	// Stores the next valid entity id.
	// This will be incremented if no free id is found.
	internal::Id next_entity_{ 0 };

	// Stores the current alive entity count (purely for retrieval).
	internal::Id count_{ 0 };

	// Vector index corresponds to the entity's id.
	// Element corresponds to whether or not the entity 
	// is currently alive.
	std::vector<bool> entities_;

	// Vector index corresponds to the entity's id.
	// Element corresponds to a flag for refreshing the entity.
	std::vector<bool> refresh_;

	// Vector index corresponds to the entity's id.
	// Element corresponds to the current version of the id.
	std::vector<internal::Version> versions_;

	// Vector index corresponds to the component's unique id.
	// If a component has not been added to a manager entity, 
	// its corresponding pool pointer will be nullptr.
	mutable std::vector<internal::BasePool*> pools_;

	// Vector index corresponds to the system's unique id.
	// If a system has not been added to a manager entity, 
	// its corresponding pointer will be nullptr.
	std::vector<internal::BaseSystem*> systems_;

	// Free list of entity ids to be used 
	// before incrementing next_entity_.
	std::deque<internal::Id> free_entities_;
};

// Entity handle object.
// Allows access to manager entities through a light-weight interface.
// Contains id, version, and a pointer to the parent manager.
class Entity {
public:
	// Null entity constructor.
	Entity() = default;

	// This constructor is not intended for use by users of the ECS library.
	// It must remain public in order to allow vector emplace construction.
	Entity(const internal::Id entity, const internal::Version version, Manager* manager) : entity_{ entity }, version_{ version }, manager_{ manager } {}

	~Entity() = default;

	// Entity handles can be moved and copied.
	Entity& operator=(const Entity&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(Entity&&) = default;

	// Entity handle comparison operators.

	bool operator==(const Entity& entity) const {
		return entity_ == entity.entity_
			&& version_ == entity.version_
			&& manager_ == entity.manager_;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}

	/*
	* Retrieves const reference to the parent manager of the entity.
	* @return Const reference to the parent manager.
	*/
	const Manager& GetManager() const {
		assert(manager_ != nullptr && "Cannot return parent manager of a null entity");
		return *manager_;
	}

	/*
	* Retrieves reference to the parent manager of the entity.
	* @return Reference to the parent manager.
	*/
	Manager& GetManager() {
		assert(manager_ != nullptr && "Cannot return parent manager of a null entity");
		return *manager_;
	}

	/*
	* Checks if an entity handle is valid.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(entity_, version_);
	}

	/*
	* Retrieves a const reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component.
	* @return Const reference to the component.
	*/
	template <typename TComponent>
	const TComponent& GetComponent() const {
		assert(IsAlive() && "Cannot retrieve component for dead or null entity");
		return manager_->GetComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Retrieves a reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component.
	* @return Reference to the component.
	*/
	template <typename TComponent>
	TComponent& GetComponent() {
		assert(IsAlive() && "Cannot retrieve component for dead or null entity");
		return manager_->GetComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Retrieves a tuple of const references to the specified component types.
	* If entity does not have components, debug assertion is called.
	* @tparam TComponent Types of components.
	* @return Tuple of const references to the requested components.
	*/
	template <typename ...TComponents>
	std::tuple<const TComponents&...> GetComponents() const {
		return std::forward_as_tuple<const TComponents&...>(GetComponent<TComponents>()...);
	}

	/*
	* Retrieves a tuple of references to the specified component types.
	* If entity does not have components, debug assertion is called.
	* @tparam TComponent Types of components.
	* @return Tuple of references to the requested components.
	*/
	template <typename ...TComponents>
	std::tuple<TComponents&...> GetComponents() {
		return std::forward_as_tuple<TComponents&...>(GetComponent<TComponents>()...);
	}

	/*
	* Adds a component to the entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param constructor_args Arguments for constructing the component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>,
					  "Cannot construct component type from given arguments");
		static_assert(internal::type_traits::is_valid_component_v<TComponent>,
					  "Cannot add component which is not move-constructable or destructable");
		assert(IsAlive() && "Cannot add component to dead or null entity");
		return manager_->AddComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>(), std::forward<TArgs>(constructor_args)...);
	}

	/*
	* Checks if entity has a component.
	* @tparam TComponent Type of component to check.
	* @return True is entity has component, false otherwise.
	*/
	template <typename TComponent>
	bool HasComponent() const {
		return IsAlive() && manager_->HasComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Checks if entity has all the given components.
	* @tparam TComponents Types of components to check.
	* @return True if entity has each component, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents() const {
		return IsAlive() && manager_->HasComponents<TComponents...>(entity_);
	}

	/*
	* Removes a component from the entity.
	* If entity does not have component type, nothing happens.
	* @tparam TComponent Type of component to remove.
	*/
	template <typename TComponent>
	void RemoveComponent() {
		assert(IsAlive() && "Cannot remove component from dead or null entity");
		manager_->RemoveComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Removes multiple components from the entity.
	* If entity does not have a component type, it is ignored.
	* @tparam TComponents Types of components to remove.
	*/
	template <typename ...TComponents>
	void RemoveComponents() const {
		assert(IsAlive() && "Cannot remove components from dead or null entity");
		manager_->RemoveComponents<TComponents...>(entity_);
	}

	// Removes all components from the entity.
	void RemoveComponents() {
		assert(IsAlive() && "Cannot remove all components from dead or null entity");
		return manager_->RemoveComponents(entity_);
	}

	// Marks the entity for destruction.
	// Note that the entity will remain alive and valid 
	// until Refresh() is called on its parent manager.
	// If entity is already marked for deletion, nothing happens.
	void Destroy() {
		if (IsAlive()) {
			manager_->DestroyEntity(entity_, version_);
		}
	}
private:
	/*
	* Ids are numbers by which entities are identified in the manager.
	* @return Id of the entity.
	*/
	internal::Id GetId() const {
		return entity_;
	}

	/*
	* Versions are numbers which represent how many times an entity
	* has been reused in the manager.
	* @return Version of the entity.
	*/
	internal::Version GetVersion() const {
		return version_;
	}

	// Custom hash function requires access to id, version, and manager.
	friend struct std::hash<Entity>;

	// NullEntity comparison uses versions so it requires private access.
	friend class NullEntity;

	// Id associated with an entity in the manager.
	internal::Id entity_{ 0 };

	// Version counter to check if handle has been invalidated.
	internal::Version version_{ internal::null_version };

	// Parent manager pointer for calling handle functions.
	Manager* manager_{ nullptr };
};

// Null entity object.
// Allows for comparing invalid / uninitialized 
// entities with valid manager created entities.
class NullEntity {
public:
	// Implicit conversion to entity object.
	operator Entity() const {
		return Entity{};
	}

	// Constexpr comparisons of null entities to each other.

	constexpr bool operator==(const NullEntity&) const {
		return true;
	}
	constexpr bool operator!=(const NullEntity&) const {
		return false;
	}

	// Comparison to entity objects.

	bool operator==(const Entity& entity) const {
		return entity.version_ == internal::null_version;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}
};

// Entity comparison with null entity.

inline bool operator==(const Entity& entity, const NullEntity& null_entity) {
	return null_entity == entity;
}
inline bool operator!=(const Entity& entity, const NullEntity& null_entity) {
	return !(null_entity == entity);
}

// Null entity.
// Allows for comparing invalid / uninitialized 
// entities with valid manager created entities.
inline constexpr NullEntity null{};

inline Entity Manager::CreateEntity() {
	internal::Id entity{ 0 };
	// Pick entity from free list before trying to increment entity counter.
	if (free_entities_.size() > 0) {
		entity = free_entities_.front();
		free_entities_.pop_front();
	} else {
		entity = next_entity_++;
	}
	// Double the size of the manager if capacity is reached.
	if (entity >= entities_.size()) {
		Resize(versions_.capacity() * 2);
	}
	assert(entity < entities_.size());
	assert(!entities_[entity] && "Cannot create new entity from live entity");
	assert(!refresh_[entity] && "Cannot create new entity from refresh marked entity");
	// Mark entity for refresh.
	refresh_[entity] = true;
	return Entity{ entity, ++versions_[entity], this };
}

template <typename ...TComponents>
inline std::vector<Entity> Manager::GetEntitiesWith() {
	std::vector<Entity> entities;
	entities.reserve(next_entity_);
	assert(entities_.size() == versions_.size());
	assert(next_entity_ <= entities_.size());
	// Cache component pools.
	auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			bool has_components = {
						(std::get<internal::Pool<TComponents>*>(pools)->Has(entity) && ...)
			};
			if (has_components) {
				entities.emplace_back(entity, versions_[entity], this);
			}
		}
	}
	return entities;
}

template <typename ...TComponents>
inline std::vector<Entity> Manager::GetEntitiesWithout() {
	std::vector<Entity> entities;
	entities.reserve(next_entity_);
	assert(entities_.size() == versions_.size());
	assert(next_entity_ <= entities_.size());
	// Cache component pools.
	auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			bool does_not_have_components = {
						(!std::get<internal::Pool<TComponents>*>(pools)->Has(entity) || ...)
			};
			if (does_not_have_components) {
				entities.emplace_back(entity, versions_[entity], this);
			}
		}
	}
	return entities;
}

inline std::vector<Entity> Manager::GetEntities() {
	std::vector<Entity> entities;
	entities.reserve(next_entity_);
	assert(entities_.size() == versions_.size());
	assert(next_entity_ <= entities_.size());
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			entities.emplace_back(entity, versions_[entity], this);
		}
	}
	return entities;
}

template <typename ...TComponents>
inline void Manager::DestroyEntitiesWith() {
	assert(entities_.size() == refresh_.size());
	assert(next_entity_ <= entities_.size());
	// Cache component pools.
	auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			bool has_components = {
						(std::get<internal::Pool<TComponents>*>(pools)->Has(entity) && ...)
			};
			if (has_components) {
				refresh_[entity] = true;
			}
		}
	}
}

template <typename ...TComponents>
inline void Manager::DestroyEntitiesWithout() {
	assert(entities_.size() == refresh_.size());
	assert(next_entity_ <= entities_.size());
	// Cache component pools.
	auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			bool does_not_have_components = {
						(!std::get<internal::Pool<TComponents>*>(pools)->Has(entity) || ...)
			};
			if (does_not_have_components) {
				refresh_[entity] = true;
			}
		}
	}
}

inline void Manager::DestroyEntities() {
	assert(entities_.size() == refresh_.size());
	assert(next_entity_ <= entities_.size());
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			refresh_[entity] = true;
		}
	}
}

template <typename ...TComponents>
inline std::vector<std::tuple<Entity, TComponents&...>> Manager::GetComponentTuple() {
	std::vector<std::tuple<Entity, TComponents&...>> vector_of_tuples;
	if (count_ > 0) {
		auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
		bool manager_has_components = {
			((std::get<internal::Pool<TComponents>*>(pools) != nullptr) && ...)
		};
		// Cycle through all manager entities.
		if (manager_has_components) {
			vector_of_tuples.reserve(next_entity_);
			assert(entities_.size() == versions_.size());
			assert(next_entity_ <= entities_.size());
			// Cycle through all manager entities.
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				if (entities_[entity]) {
					bool has_components = {
						(std::get<internal::Pool<TComponents>*>(pools)->Has(entity) && ...)
					};
					if (has_components) {
						vector_of_tuples.emplace_back(Entity{ entity, versions_[entity], this }, (*std::get<internal::Pool<TComponents>*>(pools)->Get(entity))...);
					}
				}
			}
		}
	}
	return vector_of_tuples;
}

template <typename ...TRequiredComponents>
inline void System<TRequiredComponents...>::SetComponentDependencies() {
	std::array<internal::Id, sizeof...(TRequiredComponents)> components{ Manager::GetComponentId<TRequiredComponents>()... };
	if (components.size() > 0) {
		auto size{ *std::max_element(components.begin(), components.end()) + 1 };
		components_.reserve(size);
		components_.resize(size, false);
		for (auto component : components) {
			assert(component < components_.size());
			components_[component] = true;
		}
	}
}

template <typename ...TRequiredComponents>
inline void System<TRequiredComponents...>::ResetCacheIfFlagged() {
	if (reset_required_) {
		entities = manager_->GetComponentTuple<TRequiredComponents...>();
		reset_required_ = false;
	}
}

} // namespace ecs

namespace std {

// Custom hashing function for ecs::Entity class allows for use of unordered maps and sets with entities as keys
template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& k) const {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<ecs::Manager*>()(k.manager_);
		h = h * 31 + std::hash<ecs::internal::Id>()(k.entity_);
		h = h * 31 + std::hash<ecs::internal::Version>()(k.version_);
		return h;
	}
};

} // namespace std
