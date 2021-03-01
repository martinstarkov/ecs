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
#include <vector> // std::vector
#include <array> // std::array
#include <deque> // std::deque
#include <tuple> // std::tuple
#include <functional> // std::hash
#include <algorithm> // std::max_element
#include <type_traits> // std::enable_if, std::is_destructible_v, std::is_base_of_v, etc
#include <cassert> // assert

namespace ecs {

// Forward declarations.

class Entity;
class NullEntity;
class Manager;
class BasePool;
class BaseSystem;
template <typename ...TComponents>
class System;

namespace internal {

namespace type_traits {

/*
* @tparam TComponent The component type to check pool validity for.
*/
template <typename TComponent>
using is_valid_pool_t = std::enable_if_t<std::is_destructible_v<TComponent>, bool>;

/*
* @tparam T The type to check constructability for.
* @tparam TArgs The constructor argument types for T.
*/
template <typename T, typename ...TArgs>
using is_constructible_t = std::enable_if_t<std::is_constructible_v<T, TArgs...>, bool>;

template <typename ...TArgs>
struct pack {};

template <typename>
struct strip {};

template <typename ...TArgs>
struct strip<pack<TArgs...>> {
	using type = System<TArgs...>;
};

template <typename T, class R = void>
struct enable_if_type { using type = R; };

template <typename T, typename Enable = void>
struct has_components_alias {
	static const bool value = false;
};

template <typename T>
struct has_components_alias<T, typename enable_if_type<typename T::Components>::type> {
	static const bool value = std::is_base_of_v<strip<T::Components>::type, T>;
};

template <typename TSystem>
constexpr bool is_valid_system_v{ has_components_alias<TSystem>::value };

template <typename TSystem>
using is_valid_system_t = std::enable_if_t<is_valid_system_v<TSystem>, bool>;

} // namespace type_traits

// Aliases.

using Id = std::uint32_t;
using Version = std::uint32_t;
using Offset = std::uint32_t;

// Constants.

// Represents an invalid version number.
constexpr Version null_version{ 0 };

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual BasePool* Clone() const = 0;
	virtual bool Remove(const Id entity) = 0;
	virtual void Reset() = 0;
	virtual const std::tuple<Offset, Offset, const std::vector<Offset>&, const std::deque<Offset>&> GetVariables() const = 0;
};

/*
* @tparam TComponent The component type of the pool.
*/
template <typename TComponent, type_traits::is_valid_pool_t<TComponent> = true>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		Allocate(1);
	}

	~Pool() {
		Deallocate();
	}

	// Component pools should never be copied.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	// Move operator used for resizing a vector of component pools (in the manager class).
	Pool(Pool&& obj) noexcept :
		pool_{ obj.pool_ },
		capacity_{ obj.capacity_ },
		size_{ obj.size_ },
		offsets_{ std::exchange(obj.offsets_, {}) },
		available_offsets_{ std::exchange(obj.available_offsets_, {}) } {
		obj.pool_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	Pool& operator=(Pool&&) = delete;

	/*
	* @return Pointer to an identical component pool.
	*/
	virtual BasePool* Clone() const override final {
		TComponent* new_block = nullptr;
		// Copy entire pool block over to new pool.
		std::memcpy(new_block, pool_, capacity_ * sizeof(TComponent));
		return new Pool<TComponent>(new_block, capacity_, size_, offsets_, available_offsets_);
	}

	// Resets memory block and all pool variables.
	// Equivalent to constructing an entirely new pool.
	virtual void Reset() override final {
		Deallocate();
		capacity_ = 0;
		size_ = 0;
		offsets_.clear();
		offsets_.shrink_to_fit();
		available_offsets_.clear();
		available_offsets_.shrink_to_fit();
		Allocate(1);
	}

	/*
	* @param entity Id of the entity to remove a component for.
	* @return True if component was removed, false otherwise.
	*/
	virtual bool Remove(const Id entity) override final {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != 0) {
				TComponent* address = pool_ + (offset - 1);
				// Call destructor on component memory location.
				address->~TComponent();
				available_offsets_.emplace_back(offset);
				// Set offset to invalid.
				offset = 0;
				return true;
			}
		}
		return false;
	}

	/*
	* Create / replace a component in the pool.
	* @tparam TArgs Types of constructor arguments.
	* @param entity Id of the entity for the added component.
	* @param constructor_args Arguments to be passed to the component constructor.
	* @return Pointer to the newly added / replaced component.
	*/
	template <typename ...TArgs, type_traits::is_constructible_t<TComponent, TArgs...> = true>
	TComponent* Add(const Id entity, TArgs&&... constructor_args) {
		auto offset = GetAvailableOffset();
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		if (static_cast<std::size_t>(entity) >= offsets_.size()) {
			offsets_.resize(static_cast<std::size_t>(entity) + 1, 0);
		}
		bool replace = offsets_[entity] != 0;
		offsets_[entity] = offset;
		TComponent* address = pool_ + (offset - 1);
		// Call destructor on potential previous components
		// at the address.
		assert(address != nullptr);
		if (replace) {
			address->~TComponent();
		}
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		return address;
	}

	/*
	* @param Id of the entity to check a component for.
	* @return entity True if the pool contains a valid offset, false otherwise.
	*/
	bool Has(const Id entity) const {
		return entity < offsets_.size() && offsets_[entity] != 0;
	}

	/*
	* @param Id of the entity to retrieve a component for.
	* @return entity The memory location of a component, nullptr if it does not exist.
	*/
	TComponent* Get(const Id entity) {
		if (Has(entity)) {
			return pool_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}

	virtual const std::tuple<Offset, Offset, const std::vector<Offset>&, const std::deque<Offset>&> GetVariables() const override final {
		return std::make_tuple(size_, capacity_, offsets_, available_offsets_);
	}
private:

	// Constructor used for cloning pools.
	Pool(TComponent* pool,
		 const Offset capacity,
		 const Offset size,
		 const std::vector<Offset>& offsets,
		 const std::deque<Offset>& available_offsets) :
		pool_{ pool },
		capacity_{ capacity },
		size_{ size },
		offsets_{ offsets },
		available_offsets_{ available_offsets } {}

	/*
	* Allocate some initial amount of memory for the pool.
	* @param starting_capacity The starting capacity of the pool
	* (number of components it will support to begin with).
	*/
	void Allocate(const Offset starting_capacity) {
		assert(pool_ == nullptr);
		assert(capacity_ == 0);
		assert(size_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		pool_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		assert(pool_ != nullptr && "Could not properly allocate memory for component pool");
	}

	// Destroys all the components in the offset array and frees the pool.
	void Deallocate() {
		// Call destructor of all addresses with valid offsets.
		for (auto offset : offsets_) {
			if (offset != 0) {
				TComponent* address = pool_ + (offset - 1);
				address->~TComponent();
			}
		}
		assert(pool_ != nullptr && "Cannot free invalid component pool pointer");
		// Free the allocated pool memory block.
		std::free(pool_);
		pool_ = nullptr;
	}

	/*
	* Double the capacity of a pool if the current capacity is exceeded.
	* @param new_size Desired size of the pool
	* (minimum number of components it should support).
	*/
	void ReallocateIfNeeded(const Offset new_size) {
		if (new_size >= capacity_) {
			// Double the capacity.
			capacity_ = new_size * 2;
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			pool_ = static_cast<TComponent*>(std::realloc(pool_, capacity_ * sizeof(TComponent)));
		}
	}

	/*
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset can be generated.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset next_offset{ 0 };
		if (available_offsets_.size() > 0) {
			// Take offset from the front of the free offsets.
			// This better preserves component locality as
			// components are pooled (pun) in the front.
			next_offset = available_offsets_.front();
			available_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			next_offset = ++size_;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_offset != 0 && "Could not find a valid offset from component pool");
		return next_offset;
	}

	// Pointer to the beginning of the pool's memory block.
	TComponent* pool_{ nullptr };

	// Component capacity of the pool.
	Offset capacity_{ 0 };

	// Number of components currently in the pool.
	Offset size_{ 0 };

	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the pool_ pointer.
	std::vector<Offset> offsets_;

	// List of free offsets (avoid reallocating entire pool block).
	// Choice of double ended queue as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> available_offsets_;
};

class BaseSystem {
public:
	friend class Manager;
	virtual void Update() = 0;
	virtual ~BaseSystem() = default;
private:
	virtual BaseSystem* Clone() const = 0;
	virtual void Reset() = 0;
	virtual void FlagForResetIfDependsOn(const internal::Id component) = 0;
	virtual void FlagForReset() = 0;
	virtual void ResetIfFlagged() = 0;
	virtual const std::tuple<const std::vector<bool>&, bool> GetVariables() const = 0;
};

} // namespace internal

/*
* @tparam TComponents The component types required for entities in the system.
*/
template <typename ...TComponents>
class System : public internal::BaseSystem {
public:
	template <typename T>
	friend struct has_components_alias;
	virtual void Update() override {}
	System() = default;
	virtual ~System() = default;
	using Components = internal::type_traits::pack<TComponents...>;
protected:

	Manager& GetManager() {
		assert(manager_ != nullptr && "Cannot retrieve manager for uninitialized system");
		return *manager_;
	}
	std::vector<std::tuple<Entity, TComponents&...>> entities;
private:
	System(const std::vector<bool>& components,
		   Manager* manager,
		   bool reset_cache) :
		components_{ components },
		manager_{ manager },
		reset_cache_{ reset_cache } {}
	/*
	* @return Pointer to an identical system.
	*/
	virtual BaseSystem* Clone() const override final {
		return new System<TComponents...>(components_, manager_, reset_cache_);
	}

	virtual void Reset() override final {
		reset_cache_ = true;
		ResetIfFlagged();
	}

	virtual const std::tuple<const std::vector<bool>&, bool> GetVariables() const override final {
		return std::make_tuple(components_, reset_cache_);
	}

	friend class Manager;

	virtual void FlagForResetIfDependsOn(const internal::Id component) {
		if (DependsOn(component)) {
			reset_cache_ = true;
		}
	}

	virtual void FlagForReset() {
		reset_cache_ = true;
	}

	virtual void ResetIfFlagged() override final;

	void SetComponentDependencies();

	bool DependsOn(const internal::Id component) const {
		return components_.size() == 0 || (component < components_.size() && components_[component]);
	}

	void Init(Manager* manager) {
		manager_ = manager;
		assert(manager_ != nullptr && "Cannot initialize system with null parent manager");
		ResetIfFlagged();
	}

	std::vector<bool> components_;
	Manager* manager_{ nullptr };
	bool reset_cache_{ true };
};

class Manager {
public:
	Manager() {
		// Reserve capacity for 1 entity so
		// size will double in powers of 2.
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
		DestroyPools();
		DestroySystems();
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
	* It is not advisable to use this in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition is identical, false otherwise.
	*/
	bool operator==(const Manager& other) const {
		return next_entity_ == other.next_entity_
			&& entities_ == other.entities_
			&& versions_ == other.versions_
			&& free_entities_ == other.free_entities_
			&& refresh_ == other.refresh_
			&& std::equal(std::begin(systems_), std::end(systems_),
						  std::begin(other.systems_), std::end(other.systems_),
						  [](const internal::BaseSystem* lhs, const internal::BaseSystem* rhs) {
			return lhs && rhs && lhs->GetVariables() == rhs->GetVariables(); })
			&& std::equal(std::begin(pools_), std::end(pools_),
						  std::begin(other.pools_), std::end(other.pools_),
						  [](const internal::BasePool* lhs, const internal::BasePool* rhs) {
				return lhs && rhs && lhs->GetVariables() == rhs->GetVariables(); });
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use this in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition differs, false otherwise.
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
		// id_ already set to next available one inside default constructor.
		clone.next_entity_ = next_entity_;
		clone.entities_ = entities_;
		clone.refresh_ = refresh_;
		clone.versions_ = versions_;
		clone.free_entities_ = free_entities_;
		clone.systems_.resize(systems_.size(), nullptr);
		for (std::size_t i = 0; i < systems_.size(); ++i) {
			auto system = systems_[i];
			if (system != nullptr) {
				clone.systems_[i] = system->Clone();
			}
		}
		clone.pools_.resize(pools_.size(), nullptr);
		for (std::size_t i = 0; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool != nullptr) {
				clone.pools_[i] = pool->Clone();
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
				pool->Reset();
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
		internal::Id count = count_;
		for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
			// Entity was marked for refresh.
			if (refresh_[entity]) {
				refresh_[entity] = false;
				if (entities_[entity]) { // Marked for deletion.
					RemoveComponents(entity);
					entities_[entity] = false;
					++versions_[entity];
					free_entities_.emplace_back(entity);
					--count;
				} else { // Marked for 'creation'.
					entities_[entity] = true;
					++count;
				}
			}
		}
		assert(count >= 0);
		count_ = count;
	}

	// Clears entity cache and destroys component pools.
	// Resets entity capacity to 0.
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
		DestroySystems();

		pools_.clear();
		pools_.shrink_to_fit();
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
		delete GetSystem<TSystem>(system_id);
		using SystemType = typename internal::type_traits::strip<TSystem::Components>::type;
		auto system = new SystemType();
		assert(system != nullptr && "Could not create new system correctly");
		system->SystemType::SetComponentDependencies();
		system->SystemType::Init(this);
		systems_[system_id] = system;
	}

	template <typename TSystem>
	void AddSystem() {
		static_assert(internal::type_traits::is_valid_system_v<TSystem>, "Cannot add a system to the manager which does not inherit from ecs::System class");
		AddSystemImpl<TSystem>();
	}

	template <typename TSystem>
	void UpdateSystemImpl(bool refresh_manager) {
		auto system_id = GetSystemId<TSystem>();
		assert(HasSystem<TSystem>() && "Cannot update a system which does not exist in manager");
		auto system = GetSystem<TSystem>(system_id);
		system->TSystem::ResetIfFlagged();
		system->TSystem::Update();
		if (refresh_manager) {
			auto manager = system->TSystem::manager_;
			assert(manager != nullptr);
			manager->Refresh();
		}
	}

	template <typename TSystem>
	void UpdateSystem(bool refresh_manager = false) {
		static_assert(internal::type_traits::is_valid_system_v<TSystem>, "Cannot update a system which does not inherit from ecs::System class");
		UpdateSystemImpl<TSystem>(refresh_manager);
	}

	template <typename TSystem>
	bool HasSystem() {
		if constexpr (!internal::type_traits::is_valid_system_v<TSystem>) {
			return false;
		}
		auto system = GetSystemId<TSystem>();
		return system < systems_.size() && systems_[system] != nullptr;
	}

	template <typename TSystem>
	void RemoveSystem() {
		if constexpr (internal::type_traits::is_valid_system_v<TSystem>) {
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
	* Retrieve a pointer to the component pool with matching component id.
	* @tparam TComponent Type of component to retrieve pool for.
	* @param component Id of component to retrieve pool for.
	* @return Pointer to the component pool, nullptr if pool does not exist.
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
	TSystem* GetSystem(const internal::Id system) const {
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
				system->FlagForResetIfDependsOn(component);
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
		auto& component_reference = *pool->Add(entity, std::forward<TArgs>(constructor_args)...);
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

	template <typename ...TComponents>
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
	// Mutable as pools are accessed from const methods.
	mutable std::vector<internal::BasePool*> pools_;

	// Vector index corresponds to the system's unique id.
	// If a system has not been added to a manager entity, 
	// its corresponding pointer will be nullptr.
	std::vector<internal::BaseSystem*> systems_;

	// Free list of entity ids to be used 
	// before incrementing next_entity_.
	std::deque<internal::Id> free_entities_;
};

class Entity {
public:
	// Null entity constructor.
	Entity() = default;

	// Valid entity constructor.
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
		return const_cast<Manager&>(static_cast<const Entity&>(*this).GetManager());
	}

	/*
	* Checks if entity handle is valid and alive.
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
		return const_cast<TComponent&>(static_cast<const Entity&>(*this).GetComponent<TComponent>());
	}

	template <typename ...TComponents>
	std::tuple<const TComponents&...> GetComponents() const {
		return std::forward_as_tuple<const TComponents&...>(GetComponent<TComponents>()...);
	}

	template <typename ...TComponents>
	std::tuple<TComponents&...> GetComponents() {
		return std::forward_as_tuple<TComponents&...>(GetComponent<TComponents>()...);
	}

	/*
	* Adds a component to the entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param constructor_args Arguments for constructing component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>, "Cannot construct component type from given arguments");
		static_assert(std::is_destructible_v<TComponent>, "Cannot add component which does not have a valid destructor");
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
		assert(IsAlive() && "Cannot check if dead or null entity has a component");
		return manager_->HasComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Check if entity has all the given components.
	* @tparam TComponents Types of components to check.
	* @return True if entity has each component, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents() const {
		assert(IsAlive() && "Cannot check if dead or null entity has components");
		return manager_->HasComponents<TComponents...>(entity_);
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

	template <typename ...TComponents>
	bool RemoveComponents() const {
		assert(IsAlive() && "Cannot remove components from dead or null entity");
		return manager_->RemoveComponents<TComponents...>(entity_);
	}

	/*
	* Removes all components from the entity.
	*/
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

	internal::Id GetId() const {
		return entity_;
	}

	internal::Version GetVersion() const {
		return version_;
	}

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

// Null entity object.
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
		Resize(entities_.capacity() * 2);
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

template <typename ...TComponents>
inline void System<TComponents...>::SetComponentDependencies() {
	std::array<internal::Id, sizeof...(TComponents)> components{ Manager::GetComponentId<TComponents>()... };
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

template <typename ...TComponents>
inline void System<TComponents...>::ResetIfFlagged() {
	if (reset_cache_) {
		entities = manager_->GetComponentTuple<TComponents...>();
		reset_cache_ = false;
	}
}

} // namespace ecs

namespace std {

// Custom hashing function for ecs::Entity class allows for use of unordered maps with entities as keys
template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& k) const {
		using std::size_t;
		using std::hash;

		// Compute individual hash values for first, second, and third 
		// then combine them using XOR and bit shifting:

		return (
			(
				hash<ecs::internal::Id>()(k.GetId()) ^
				(hash<ecs::internal::Version>()(k.GetVersion()) << 1)
				) >> 1) ^
			(hash<const ecs::Manager*>()(&k.GetManager()) << 1);
	}
};

} // namespace std