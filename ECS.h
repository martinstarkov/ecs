/*

MIT License

Copyright (c) 2022 | Martin Starkov | https://github.com/martinstarkov

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

#include <cstdlib>     // std::malloc
#include <cstdint>     // std::uint32_t
#include <vector>      // std::vector
#include <array>       // std::array
#include <deque>       // std::deque
#include <tuple>       // std::tuple
#include <functional>  // std::hash
#include <utility>     // std::function
#include <algorithm>   // std::max_element
#include <memory>      // std::allocator
#include <type_traits> // std::is_destructible_v
#include <cassert>     // assert

namespace ecs {

// Forward declarations for user-accessible types.

class Entity;
class Manager;

namespace impl {

// Forward declarations for internally used types.

class NullEntity;
class PoolInterface;

template <typename TComponent>
class Pool;

// Aliases.

/*
* Type representing the identifier of each entity within the manager.
* This id is used to index all internal storage vectors.
*/
using Id = std::uint32_t;

/*
* Type representing how many times an entity id has been reused in the manager.
* Incremented each time an entity is fully destroyed. Allows for reuse of entity ids.
*/
using Version = std::uint32_t;

// Type representing the offset of a component from the start of a memory block in a pool.
using Offset = std::uint32_t;

// Constants.

// Represents an invalid version number.
inline constexpr Version null_version{ 0 };

/* 
* Offset of 0 is considered invalid.
* In each access case 1 is subtracted so that the 0th offset is still used.
* This acts as a null offset to fill the sparse array.
*/
inline constexpr Offset invalid_offset{ 0 };

/*
* Modified container hashing from:
* https://stackoverflow.com/a/27216842
*/
template <template <typename, typename> typename H, typename S, typename T,
	std::enable_if_t<std::is_convertible_v<S, std::size_t>, bool> = true>
inline std::size_t HashContainer(const H<S, T>& container) {
	std::size_t hash{ container.size() };
	for (auto value : container) {
		hash ^= static_cast<std::size_t>(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
	}
	return hash;
};

class PoolInterface {
public:
	virtual ~PoolInterface() = default;
	virtual PoolInterface* Clone() const = 0;
	virtual Id GetComponentId() const = 0;
	virtual bool Has(Id entity) const = 0;
	virtual void Copy(Id from, Id to) = 0;
	virtual bool Remove(Id entity) = 0;
	virtual void Clear() = 0;
	virtual std::size_t Hash() const = 0;
};

/*
* Container which stores the offsets of components from the beginning of a memory block pointer.
* Offsets can be accessed using unique entity ids.
* Component must be move constructible and destructible.
* @tparam TComponent Type of component to store in the pool.
* If no types are given, system will fetch all manager entities.
*/
template <typename TComponent>
class Pool : public PoolInterface {
static_assert(std::is_move_constructible_v<TComponent>,
				"Component must be move constructible to create a pool for it");
static_assert(std::is_destructible_v<TComponent>,
				"Component must be destructible to create a pool for it");
public:
	Pool() {
		/*
		* Allocate enough memory for the first component.
		* This enables capacity to double in powers of 2.
		*/
		AllocateMemoryBlock(1);
	}

	~Pool() {
		ComponentDestructors();
		/*
		* Frees the allocated memory block associated with the pool.
		* This must be called whenever destroying a pool.
		*/
		assert(block_ && "Do not free invalid component pools");
		std::free(block_);
		block_ = nullptr;
	}

	// Pools should never be copied accidentally. Use Clone() if a copy is intended.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	// Move operator for resizing vector of component pools in Manager class.
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
	// Pools should not be move assigned.
	Pool& operator=(Pool&&) = delete;

	/*
	* Creates a duplicate (copy) pool with all the same components and offsets.
	* @return Pointer to an identical component pool.
	*/
	virtual PoolInterface* Clone() const final {
		static_assert(std::is_copy_constructible_v<TComponent>,
					  "Cannot clone component pool with a non copy-constructible component");
		// Empty memory block for clone is allocated in constructor.
		auto clone{ new Pool<TComponent>(capacity_, size_,
										 offsets_, freed_offsets_) };

		// Copy each individual component offset to the new pool block.
		for (auto offset : offsets_) {
			if (offset != invalid_offset) {
				auto adjusted_offset{ (offset - 1) };
				TComponent* component{ block_ + adjusted_offset };
				TComponent* address{ clone->block_ + adjusted_offset };
				// Copy component from current pool to 
				// other pool using copy constructor.
				new(address) TComponent(*component);
			}
		}
		return clone;
	}

	/*
	* @param from Entity id from which to copy a component.
	* @param to Entity id to component will be copied.
	*/
	virtual void Copy(Id from, Id to) final {
		static_assert(std::is_copy_constructible_v<TComponent>,
					  "Cannot copy component in a pool of non copy constructible components");
		auto offset{ GetAvailableOffset() };
		/*
		* If the entity exceeds the indexing table's size,
		* expand the indexing table with invalid offsets.
		*/
		auto to_location{ static_cast<std::size_t>(to) };
		if (to_location >= offsets_.size()) {
			offsets_.resize(to_location + 1, invalid_offset);
		}
		// Check if component offset exists already.
		bool replace{ offsets_[to_location] != invalid_offset };
		assert(!replace && "Cannot overwrite existing component while copying entity");
		offsets_[to_location] = offset;
		TComponent* address{ block_ + (offset - 1) };
		// Create the component into the new address with the given copy.
		new(address) TComponent(*Get(from));
		assert(address && "Failed to copy component to offset memory location");
	}

	/*
	* Removes the component from the given entity.
	* @param entity Id of the entity to remove a component from.
	* @return True if component was removed, false otherwise.
	*/
	virtual bool Remove(Id entity) final {
		if (entity < offsets_.size()) {
			auto& offset{ offsets_[entity] };
			if (offset != invalid_offset) {
				TComponent* address{ block_ + (offset - 1) };
				// Call destructor on component memory location.
				address->~TComponent();
				freed_offsets_.emplace_back(offset);
				offset = invalid_offset;
				return true;
			}
		}
		return false;
	}

	/*
	* Calls destructor on each component.
	* Clears free and occupied offsets.
	* Does not modify pool capacity.
	* Equivalent to clearing a standard library container.
	*/
	virtual void Clear() final {
		ComponentDestructors();
		offsets_.clear();
		freed_offsets_.clear();
		size_ = 0;
	}

	/*
	* Creates / replaces a component for an entity in the pool.
	* Component must be constructible from the given arguments.
	* @tparam TArgs Types of constructor arguments.
	* @param entity Id of the entity to add a component to.
	* @param constructor_args Arguments to be passed to the component constructor.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename ...TArgs>
	TComponent& Add(Id entity, TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>,
					  "Cannot add component to pool which is not constructible from given arguments");
		//"Cannot add component to pool which is not constructible from given arguments"
		auto offset{ GetAvailableOffset() };
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		auto location{ static_cast<std::size_t>(entity) };
		if (location >= offsets_.size()) {
			offsets_.resize(location + 1, invalid_offset);
		}
		// Check if component offset exists already.
		bool replace{ offsets_[location] != invalid_offset };
		offsets_[location] = offset;
		TComponent* address{ block_ + (offset - 1) };
		if (replace) {
			// Call destructor on potential previous components
			// at the address.
			address->~TComponent();
		}
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		assert(address && "Failed to create component at offset memory location");
		return *address;
	}

	/*
	* Checks if the entity has a component in the pool.
	* @param entity Id of the entity to check a component for.
	* @return True if the pool contains a valid component offset, false otherwise.
	*/
	virtual bool Has(Id entity) const final {
		return entity < offsets_.size() && offsets_[entity] != invalid_offset;
	}

	/*
	* Retrieves the component of a given entity from the pool.
	* @param entity Id of the entity to retrieve a component for.
	* @return Const pointer to the component, nullptr if the component does not exist.
	*/
	const TComponent* Get(Id entity) const {
		if (Pool<TComponent>::Has(entity)) {
			return block_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}

	/*
	* Retrieves the component of a given entity from the pool.
	* @param entity Id of the entity to retrieve a component for.
	* @return Pointer to the component, nullptr if the component does not exist.
	*/
	TComponent* Get(Id entity) {
		return const_cast<TComponent*>(static_cast<const Pool<TComponent>&>(*this).Get(entity));
	}

	/*
	* @return Id of the pool's component type.
	*/
	virtual Id GetComponentId() const final;

	/*
	* Generates a hash number using pool members.
	* Useful for identifying if two pools are identical.
	* @return Hash code for the pool.
	*/
	virtual std::size_t Hash() const final {
		/*
		* Hashing combination algorithm from:
		* https://stackoverflow.com/a/17017281
		*/
		std::size_t hash{ 17 };
		hash = hash * 31 + std::hash<Offset>()(size_);
		hash = hash * 31 + std::hash<Offset>()(capacity_);
		hash = hash * 31 + HashContainer(offsets_);
		hash = hash * 31 + HashContainer(freed_offsets_);
		return hash;
	}

private:
	// Constructor used for cloning identical pools.
	Pool(Offset capacity,
	     Offset size,
		 const std::vector<Offset>& offsets,
		 const std::deque<Offset>& freed_offsets) :
		/*
		* Allocate memory block before capacity is set
		* as otherwise capacity == 0 assertion fails.
		*/
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
	void AllocateMemoryBlock(Offset starting_capacity) {
		assert(!block_ && "Cannot allocate memory block overtop existing block");
		assert(!capacity_ && "Memory block must be empty before allocation");
		assert(!size_ && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		block_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		assert(block_ && "Could not properly allocate memory for component pool");
	}

	/*
	* Invokes the destructor of each valid component in the pool.
	* Note: valid offsets are not refreshed afterward.
	*/
	void ComponentDestructors() {
		for (auto offset : offsets_) {
			// Only consider valid offsets.
			if (offset != invalid_offset) {
				TComponent* address{ block_ + (offset - 1) };
				address->~TComponent();
			}
		}
	}

	/*
	* Doubles the capacity of a pool if the current capacity is exceeded.
	* @param new_size Desired size of the pool. (i.e. minimum number of components it should support).
	*/
	void ReallocateIfNeeded(Offset new_size) {
		if (new_size >= capacity_) {
			// Double the capacity each time it is reached.
			capacity_ = new_size * 2;
			assert(block_ && "Pool memory must be allocated before reallocation");
			auto new_block{ static_cast<TComponent*>(
				std::realloc(block_, capacity_ * sizeof(TComponent))) };
			assert(new_block && "Unable to reallocate sufficient memory for component pool");
			block_ = new_block;
		}
	}

	/*
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset is available.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset available_offset{ invalid_offset };
		if (freed_offsets_.size() > 0) {
			/*
			* Take offset from the front of the free offsets.
			* This better preserves component locality as they are concentrated in the front.
			*/
			available_offset = freed_offsets_.front();
			freed_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			available_offset = ++size_;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(available_offset != invalid_offset &&
			   "Could not find a valid available offset from component pool");
		return available_offset;
	}

	// Pointer to the beginning of the pool's memory block.
	TComponent* block_{ nullptr };

	// Component capacity of the pool.
	Offset capacity_{ 0 };

	// Number of components currently in the pool.
	Offset size_{ 0 };

	// Sparse set which maps entity ids to offsets from the start of the block_ pointer.
	std::vector<Offset> offsets_;

	/*
	* This allows to avoid frequent reallocation of pools.
	* Deque used as offsets are inserted in the back and popped from the front.
	*/
	std::deque<Offset> freed_offsets_;
};

} // namespace impl

// Entity and component storage class for the ECS.
class Manager {
public:
	Manager() {
		// Reserve capacity for 1 entity so that manager size will double in powers of 2.
		Reserve(1);
	}

	~Manager() {
		DestroyPools();
	}

	// Managers cannot be copied. Clone() can create a new manager with identical composition.
	Manager& operator=(const Manager&) = delete;
	Manager(const Manager&) = delete;

	Manager(Manager&& obj) noexcept :
		next_entity_{ obj.next_entity_ },
		refresh_required_{ obj.refresh_required_ },
		entities_{ std::exchange(obj.entities_, {}) },
		refresh_{ std::exchange(obj.refresh_, {}) },
		versions_{ std::exchange(obj.versions_, {}) },
		pools_{ std::exchange(obj.pools_, {}) },
		free_entities_{ std::exchange(obj.free_entities_, {}) } {
		obj.next_entity_ = 0;
	}

	Manager& operator=(Manager&& obj) noexcept {
		// Destroy previous manager internals.
		DestroyPools();
		// Move manager into current manager.
		next_entity_ = obj.next_entity_;
		refresh_required_ = obj.refresh_required_;
		entities_ = std::exchange(obj.entities_, {});
		refresh_ = std::exchange(obj.refresh_, {});
		versions_ = std::exchange(obj.versions_, {});
		pools_ = std::exchange(obj.pools_, {});
		free_entities_ = std::exchange(obj.free_entities_, {});
		// Reset state of other manager.
		obj.next_entity_ = 0;
		return *this;
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use comparison in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition is identical, false otherwise.
	*/
	bool operator==(const Manager& other) const {
		if (next_entity_ == other.next_entity_ &&
			refresh_required_ == other.refresh_required_ &&
			free_entities_ == other.free_entities_ &&
			refresh_ == other.refresh_ &&
			entities_ == other.entities_ &&
			versions_ == other.versions_) {
			// Compare manager component pools.
			auto IdenticalComponentPools = [](const impl::PoolInterface* lhs,
											  const impl::PoolInterface* rhs) {
				return lhs == rhs || lhs && rhs && lhs->Hash() == rhs->Hash();
			};
			return std::equal(std::begin(pools_),
							  std::end(pools_),
							  std::begin(other.pools_),
							  std::end(other.pools_),
							  IdenticalComponentPools);
		}
		return false;
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
	* This provides a way of replicating a manager with identical entities and components.
	* @return New manager with identical composition.
	*/
	Manager Clone() const {
		Manager clone;
		clone.next_entity_ = next_entity_;
		clone.entities_ = entities_;
		clone.refresh_ = refresh_;
		clone.refresh_required_ = refresh_required_;
		clone.versions_ = versions_;
		clone.free_entities_ = free_entities_;
		clone.pools_.reserve(pools_.size());
		clone.pools_.resize(pools_.size(), nullptr);
		for (std::size_t i{ 0 }; i < pools_.size(); ++i) {
			auto pool{ pools_[i] };
			if (pool) {
				// Clone pools over to new manager.
				clone.pools_[i] = pool->Clone();
			}
		}
		assert(clone == *this &&
			   "Cloning manager failed");
		return clone;
	}

	/* 
	* Clears entity cache and reset component pools to empty ones.
	* Keeps entity capacity unchanged.
	* Systems are not removed but caches are flagged for reset.
	*/
	void Clear() {
		next_entity_ = 0;
		refresh_required_ = false;

		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();

		for (auto pool : pools_) {
			if (pool) {
				pool->Clear();
			}
		}
	}
	
	/*
	* Cycles through all entities and destroys ones that have been marked for destruction.
	* Activates created entities (can be used in systems).
	*/
	void Refresh() {
		if (refresh_required_) {
			// This must be set before refresh starts.
			refresh_required_ = false;
			assert(entities_.size() == versions_.size() &&
				   "Refresh failed due to varying entity vector and version vector size");
			assert(entities_.size() == refresh_.size() &&
				   "Refresh failed due to varying entity vector and refresh vector size");
			assert(next_entity_ <= entities_.size() &&
				   "Next available entity must not be out of bounds of entity vector");
			impl::Id alive{ 0 };
			impl::Id dead{ 0 };
			for (impl::Id entity{ 0 }; entity < next_entity_; ++entity) {
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
			assert(alive >= 0 && dead >= 0);
			// Update entity count with net change.
			count_ += alive - dead;
			assert(count_ >= 0);
		}
	}

	/*
	* Reserve additional entity capacity for manager.
	* @param capacity Desired entity capacity of the manager.
	* If smaller than current capacity, nothing happens.
	*/
	void Reserve(std::size_t capacity) {
		entities_.reserve(capacity);
		refresh_.reserve(capacity);
		versions_.reserve(capacity);
		assert(entities_.capacity() == refresh_.capacity() &&
			   "Entity vector and refresh vector must have the same capacity");
	}

	/*
	* Equivalent to creating an entirely new manager.
	* Resets entity caches and destroys component pools.
	* Resets all capacities to 0.
	*/
	void Reset() {
		next_entity_ = 0;
		refresh_required_ = 0;

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

		Reserve(1);
	}

	/*
	* Adds a new entity to the manager and creates a handle for it.
	* @return Handle to newly created entity.
	*/
	Entity CreateEntity();

	/*
	* Creates a new entity with the given components copied from another entity.
	* If no components are provided (i.e. CopyEntity<>), all entity components will be copied.
	* @param entity Entity handle to copy components from.
	* @tparam TComponents List of component types to copy from entity, if empty copy all.
	* @return Handle to newly copied entity.
	*/
	template <typename ...TComponents>
	Entity CopyEntity(const Entity& entity);

	// Loop through each entity in the manager.
	template <typename T>
	void ForEachEntity(T function);

	// Loop through each entity in the manager with the given components.
	template <typename ...TComponents, typename T>
	void ForEachEntityWith(T function);

	// Loop through each entity in the manager without the given components.
	template <typename ...TComponents, typename T>
	void ForEachEntityWithout(T function);

	// @return The number of entities currently live in the manager.
	std::size_t GetEntityCount() const {
		return count_;
	}

	// @return The number of entities currently not live in the manager.
	std::size_t GetDeadEntityCount() const {
		return free_entities_.size();
	}
private:
	/*
	* Checks whether or not manager contains an entity with the given components.
	* @tparam TComponents List of components to check for.
	* @return True if at least one entity exists, false otherwise.
	*/
	template <typename ...TComponents>
	bool EntityExists() const;

	// Destroys and deallocates all the component pools.
	void DestroyPools() {
		for (auto pool : pools_) {
			delete pool;
		}
	}
	
	/*
	* Resize vector of entities, refresh flags, and versions.
	* If smaller than current size, nothing happens.
	* @param size Desired size of the vectors.
	*/
	void Resize(std::size_t size) {
		if (size > entities_.size()) {
			Reserve(size);
			entities_.resize(size, false);
			refresh_.resize(size, false);
			versions_.resize(size, impl::null_version);
		}
		assert(entities_.size() == versions_.size() &&
			   "Resize failed due to varying entity vector and version vector size");
		assert(entities_.size() == refresh_.size() &&
			   "Resize failed due to varying entity vector and refresh vector size");
	}

	/*
	* Marks entity for deletion for next manager refresh.
	* @param entity Id of entity to mark for deletion.
	* @param version Version of entity for handle comparison.
	*/
	void DestroyEntity(impl::Id entity, impl::Version version) {
		assert(entity < versions_.size());
		assert(entity < refresh_.size());
		if (versions_[entity] == version) {
			if (!refresh_[entity] || entities_[entity]) {
				refresh_[entity] = true;
				refresh_required_ = true;
			} else {
				/*
				* Edge case where entity is created and marked 
				* for deletion before a Refresh() has been called.
				* In this case, destroy and invalidate the entity 
				* without a Refresh() call. This is equivalent to
				* an entity which never 'officially' existed in the manager.
				*/
				RemoveComponents(entity);
				refresh_[entity] = false;
				++versions_[entity];
				free_entities_.emplace_back(entity);
			}
		}
	}

	/*
	* Checks if entity is alive in the manager.
	* Newly added entities are considered alive.
	* @param entity Id of entity to check.
	* @param version Version of entity for handle comparison.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive(impl::Id entity, impl::Version version) const {
		return 
			entity < versions_.size() &&
			versions_[entity] == version &&
			entity < entities_.size() &&
			(entities_[entity] || refresh_[entity]);
	}

	/*
	* @param entity1 First entity.
	* @param entity2 Second entity.
	* @return True if both entities have all the same components, false otherwise.
	*/
	bool HaveMatchingComponents(impl::Id entity1, impl::Id entity2) {
		for (auto pool : pools_) {
			if (pool) {
				bool has1{ pool->Has(entity1) };
				bool has2{ pool->Has(entity2) };
				// Check that one entity has a component while the other doesn't.
				if ((has1 || has2) && (!has1 || !has2)) {
					// Exit early if one non-matching component is found.
					return false;
				}
			}
		}
		return true;
	}

	/*
	* Retrieves a pointer to the component pool with matching component id.
	* @tparam TComponent Type of component to retrieve pool for.
	* @param component Id of component to retrieve pool for.
	* @return Pointer to the component pool, nullptr if pool does not exist.
	*/
	template <typename TComponent>
	impl::Pool<TComponent>* GetPool(impl::Id component) const {
		assert(component == GetComponentId<TComponent>() &&
			   "GetPool mismatch with component id");
		if (component < pools_.size()) {
			// Note that this could be nullptr if the 
			// component pool does not exist in the manager.
			return static_cast<impl::Pool<TComponent>*>(pools_[component]);
		}
		return nullptr;
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
	const TComponent& GetComponent(impl::Id entity, impl::Id component) const {
		const auto pool{ GetPool<TComponent>(component) };
		assert(pool && "Cannot retrieve component which has not been added to manager");
		const auto component_address{ pool->Get(entity) };
		/*
		* Debug tip: 
		* If you ended up here and want to find out which
		* entity triggered this assertion, follow the call stack.
		*/
		assert(component_address && "Cannot get a component which an entity does not have");
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
	TComponent& GetComponent(impl::Id entity, impl::Id component) {
		return const_cast<TComponent&>(static_cast<const Manager&>(*this).GetComponent<TComponent>(entity, component));
	}

	/*
	* Returns whether or not an entity has the component type.
	* @tparam TComponent Type of component to check.
	* @param entity Id of entity to check component for.
	* @param component Id of component to check.
	* @return True if entity has the component, false otherwise.
	*/
	template <typename TComponent>
	bool HasComponent(impl::Id entity, impl::Id component) const {
		const auto pool{ GetPool<TComponent>(component) };
		return pool && pool->impl::Pool<TComponent>::Has(entity);
	}

	/*
	* Returns whether or not an entity has all the given component types.
	* @tparam TComponents Types of components to check.
	* @param entity Id of entity to check components for.
	* @return True if entity has each component, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents(impl::Id entity) const {
		return { (HasComponent<TComponents>(entity, GetComponentId<TComponents>()) && ...) };
	}

	/*
	* Adds a component to the specified entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param entity Id of entity to add component to.
	* @param component Id of component to add.
	* @param constructor_args Arguments for constructing component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(impl::Id entity,
							 impl::Id component,
							 TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>,
					  "Cannot add component which is not constructible from given arguments");
		static_assert(std::is_move_constructible_v<TComponent>,
					  "Cannot add component which is not move constructible");
		static_assert(std::is_destructible_v<TComponent>,
					  "Cannot add component which is not destructible");
		// Increase pool vector size based on component id.
		if (component >= pools_.size()) {
			pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
		}
		auto pool{ GetPool<TComponent>(component) };
		bool new_pool{ !pool };
		// If component type has not been added to manager,
		// generate a new pool for the given type.
		if (new_pool) {
			pool = new impl::Pool<TComponent>();
			pools_[component] = pool;
		}
		assert(pool && "Could not create new component pool correctly");
		bool new_component{ !pool->impl::Pool<TComponent>::Has(entity) };
		auto& component_reference{ pool->Add(entity, std::forward<TArgs>(constructor_args)...) };
		// If component or pool is new, possibility to trigger events here in the future.
		if (new_pool || new_component) {
			// ComponentChange(entity, component);
		}
		return component_reference;
	}

	/*
	* Removes a component from the specified entity.
	* If entity does not have the component type, nothing happens.
	* @tparam TComponent Type of component to remove.
	* @param entity Id of entity to remove component from.
	* @param component Id of component to remove.
	*/
	template <typename TComponent>
	void RemoveComponent(impl::Id entity, impl::Id component) {
		auto pool{ GetPool<TComponent>(component) };
		if (pool) {
			// Static call to derived component pool class (no dynamic dispatch).
			bool removed{ pool->impl::Pool<TComponent>::Remove(entity) };
			// If component was successfully removed, possibility to trigger event here in the future.
			if (removed) {
				// ComponentChange(entity, component);
			}
		}
	}

	/*
	* Removes multiple components from the specified entity.
	* If entity does not a component type, it is ignored.
	* @tparam TComponents Types of components to remove.
	* @param entity Id of entity to remove components from.
	*/
	template <typename ...TComponents>
	void RemoveComponents(impl::Id entity) {
		(RemoveComponent<TComponents>(entity, GetComponentId<TComponents>()), ...);
	}

	/*
	* Removes all components associated with an entity.
	* Results in virtual function call on each component pool.
	* @param entity Id of entity to remove all components from.
	*/
	void RemoveComponents(impl::Id entity) {
		for (impl::Id i{ 0 }; i < pools_.size(); ++i) {
			auto pool{ pools_[i] };
			if (pool) {
				bool removed{ pool->Remove(entity) };
				if (removed) {
					// ComponentChange(entity, i);
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
	static impl::Id GetComponentId() {
		// Get the next available id save that id as static variable for the component type.
		static impl::Id id{ ComponentCount()++ };
		return id;
	}

	/*
	* Important design decision: Component ids shared among all created managers
	* I.e. struct Position has id '3' in all manager instances, as opposed to
	* the order in which a component is first added to each manager.
	* @return Next available component id.
	*/
	static impl::Id& ComponentCount() {
		static impl::Id id{ 0 };
		return id;
	}

	template <typename TComponent>
	friend class impl::Pool;

	/*
	* Entity handles must have access to internal functions.
	* This because ids are internal and (mostly) hidden from the user.
	*/
	friend class Entity;

	// Stores the next valid entity id. This will be incremented if no free id is found.
	impl::Id next_entity_{ 0 };

	// Stores the current alive entity count (purely for retrieval).
	impl::Id count_{ 0 };

	// Stores whether or not a refresh is required in the manager.
	bool refresh_required_{ false };
	
	/*
	* Vector index corresponds to the entity's id.
	* Element corresponds to whether or not the entity is currently alive.
	*/
	std::vector<bool> entities_;
	
	/*
	* Vector index corresponds to the entity's id.
	* Element corresponds to a flag for refreshing the entity.
	*/
	std::vector<bool> refresh_;

	// Vector index corresponds to the entity's id.
	// Element corresponds to the current version of the id.
	std::vector<impl::Version> versions_;

	/*
	* Vector index corresponds to the component's unique id.
	* If a component has not been added to a manager entity, 
	* its corresponding pool pointer will be nullptr.
	* Mutable because GetPool() is called in const and non-const
	* methods and returns a pointer to a given pool.
	*/
	mutable std::vector<impl::PoolInterface*> pools_;

	// Free list of entity ids to be used before incrementing next_entity_.
	std::deque<impl::Id> free_entities_;
};

/*
* Entity handle object.
* Allows access to manager entities through a light-weight interface.
* Contains id, version, and a pointer to the parent manager.
*/
class Entity {
public:
	// Null entity construction.
	Entity() = default;

	// Entity handles are wrappers and hence default-destructible.

	~Entity() = default;

	// Entity handles can be moved and copied.

	Entity& operator=(const Entity&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(Entity&&) = default;

	// Entity handle comparison operators.

	// Returns true if entity handle is the same as the passed one.
	bool operator==(const Entity& entity) const {
		return
			entity_ == entity.entity_ &&
			version_ == entity.version_ &&
			manager_ == entity.manager_;
	}

	// Returns true if entity handle is not the same as the passed one.
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}

	// Returns true if both entities have the same components.
	bool IsIdenticalTo(const Entity& entity) const;

	/*
	* Retrieves const reference to the parent manager of the entity.
	* @return Const reference to the parent manager.
	*/
	const Manager& GetManager() const {
		assert(manager_ && "Cannot return parent manager of a null entity");
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
					  "Cannot add component which is not constructible from given arguments");
		static_assert(std::is_move_constructible_v<TComponent>,
					  "Cannot add component which is not move constructible");
		static_assert(std::is_destructible_v<TComponent>,
					  "Cannot add component which is not destructible");
		assert(IsAlive() && "Cannot add component to dead or null entity");
		return manager_->AddComponent<TComponent>(entity_,
												  manager_->GetComponentId<TComponent>(),
												  std::forward<TArgs>(constructor_args)...);
	}

	/*
	* Checks if entity has a component.
	* @tparam TComponent Type of component to check.
	* @return True is entity has component, false otherwise.
	*/
	template <typename TComponent>
	bool HasComponent() const {
		return 
			IsAlive() &&
			manager_->HasComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
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
	* If entity does not have a component type, it is skipped.
	* @tparam TComponents Types of components to remove.
	*/
	template <typename ...TComponents>
	void RemoveComponents() {
		assert(IsAlive() && "Cannot remove components from dead or null entity");
		manager_->RemoveComponents<TComponents...>(entity_);
	}

	// Removes all components from the entity.
	void RemoveComponents() {
		assert(IsAlive() && "Cannot remove all components from dead or null entity");
		return manager_->RemoveComponents(entity_);
	}

	/*
	* Marks the entity for destruction.
	* Note that the entity will remain alive and valid 
	* until Refresh() is called on its parent manager.
	* If entity is already marked for deletion, nothing happens.
	*/
	void Destroy() {
		if (IsAlive()) {
			manager_->DestroyEntity(entity_, version_);
		}
	}

	/*
	* Checks if an entity handle is valid.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive() const {
		return manager_ && manager_->IsAlive(entity_, version_);
	}
private:
	// Manager requires access to id and versions.
	friend class Manager;

	// Custom hash function requires access to id, version, and manager.
	friend struct std::hash<Entity>;

	// NullEntity comparison uses versions so it requires private access.
	friend class impl::NullEntity;

	// Actual constructor for creating entities through the manager.
	Entity(impl::Id entity, impl::Version version, Manager* manager) :
		entity_{ entity },
		version_{ version },
		manager_{ manager } {
	}

	// Id associated with an entity in the manager.
	impl::Id entity_{ 0 };

	// Version counter to check if handle has been invalidated.
	impl::Version version_{ impl::null_version };

	// Parent manager pointer for calling handle functions.
	Manager* manager_{ nullptr };
};

namespace impl {

/*
* Null entity object.
* Allows for comparing invalid / uninitialized entities with valid manager created entities.
*/
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
		return entity.version_ == impl::null_version;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}
};

} // namespace impl

/*
* Null entity.
* Allows for comparing invalid / uninitialized 
* entities with valid manager created entities.
*/
inline constexpr impl::NullEntity null{};

/*
* Below are the definitions of functions which
* require access to methods of other classes.
* Therefore they must be placed below all classes.
*/

template <typename TComponent>
inline impl::Id impl::Pool<TComponent>::GetComponentId() const {
	return ecs::Manager::GetComponentId<TComponent>();
}

inline bool Entity::IsIdenticalTo(const Entity& entity) const {
	return 
		 *this == entity ||
		 *this == ecs::null &&
		 entity == ecs::null ||
		 *this != ecs::null && 
		 entity != ecs::null &&
		 manager_ == entity.manager_ &&
		 manager_ &&
		 entity_ != entity.entity_ &&
		 manager_->HaveMatchingComponents(entity_, entity.entity_);
}

inline Entity Manager::CreateEntity() {
	impl::Id entity{ 0 };
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
	assert(entity < entities_.size() &&
		   "Created entity is outside of manager entity vector range");
	assert(!entities_[entity] &&
		   "Cannot create new entity from live entity");
	assert(!refresh_[entity] &&
		   "Cannot create new entity from refresh marked entity");
	// Mark entity for refresh.
	refresh_[entity] = true;
	refresh_required_ = true;
	return Entity{ entity, ++versions_[entity], this };
}

template <typename ...TComponents>
inline Entity Manager::CopyEntity(const Entity& entity) {
	// Create new entity in the manager to copy to.
	auto copy_entity{ CreateEntity() };
	auto from{ entity.entity_ };
	auto to{ copy_entity.entity_ };
	if constexpr (sizeof...(TComponents) > 0) {
		static_assert(std::conjunction_v<std::is_copy_constructible<TComponents>...>,
					  "Cannot copy entity with a component that is not copy constructible");
		// Copy only specific components.
		auto pools{ 
			std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...) };
		bool manager_has_components{
			((std::get<impl::Pool<TComponents>*>(pools)) && ...) };
		assert(manager_has_components && 
			   "Cannot copy entity with a component that is not even in the manager");
		bool has_components{
			(std::get<impl::Pool<TComponents>*>(pools)->impl::Pool<TComponents>::Has(from) && ...) };
		assert(has_components &&
			   "Cannot copy entity with a component that it does not have");
		(std::get<impl::Pool<TComponents>*>(pools)->impl::Pool<TComponents>::Copy(from, to), ...);
		(ComponentChange(to, GetComponentId<TComponents>()), ...);
	} else {
		// Copy all components.
		for (auto pool : pools_) {
			if (pool) {
				if (pool->Has(from)) {
					pool->Copy(from, to);
					// Trigger component change event (possibly in the future?).
					// ComponentChange(to, pool->GetComponentId());
				}
			}
		}
	}
	return copy_entity;
}

template <typename ...TComponents>
inline bool Manager::EntityExists() const {
	// Cache component pools.
	auto pools{
		std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...) };
	bool manager_has_components{
		((std::get<impl::Pool<TComponents>*>(pools)) && ...) };
	if (manager_has_components) {
		// Cycle through all manager entities.
		for (impl::Id entity_id{ 0 }; entity_id < next_entity_; ++entity_id) {
			// If entity is alive, check for components.
			if (entities_[entity_id]) {
				bool has_components{
					(std::get<impl::Pool<TComponents>*>(pools)->Has(entity_id) && ...) };
				// If unique entity with components exists, return true.
				if (has_components) {
					return true;
				}
			}
		}
	}
	return false;
}

template <typename T>
inline void Manager::ForEachEntity(T function) {
	assert(entities_.size() == versions_.size() &&
		   "Cannot loop through manager entities if and version and entity vectors differ in size");
	assert(next_entity_ <= entities_.size() &&
		   "Last entity must be within entity vector range");
	// Cycle through all manager entities.
	for (impl::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, call lambda on it.
		if (entities_[entity]) {
			function(Entity{ entity, versions_[entity], this });
		}
	}
}

template <typename ...TComponents, typename T>
inline void Manager::ForEachEntityWith(T function) {
	static_assert(sizeof ...(TComponents) > 0,
				  "Cannot loop through each entity without providing at least one component type");
	assert(entities_.size() == versions_.size() &&
		   "Cannot loop through manager entities if and version and entity vectors differ in size");
	assert(next_entity_ <= entities_.size() &&
		   "Last entity must be within entity vector range");
	auto pools{
			std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...) };
	// Check that none of the requested component pools are null.
	bool manager_has_components{
		((std::get<impl::Pool<TComponents>*>(pools)) && ...) };
	if (manager_has_components) {
		// Cycle through all manager entities.
		for (impl::Id entity{ 0 }; entity < next_entity_; ++entity) {
			// If entity is alive, call lambda on it.
			if (entities_[entity]) {
				bool has_components{
					(std::get<impl::Pool<TComponents>*>(pools)->impl::Pool<TComponents>::Has(entity) && ...) };
				if (has_components) {
					function(
						Entity{ entity, versions_[entity], this },
						(*std::get<impl::Pool<TComponents>*>(pools)->Get(entity))...
					);
				}
			
			}
		}
	}
}

template <typename ...TComponents, typename T>
inline void Manager::ForEachEntityWithout(T function) {
	assert(entities_.size() == versions_.size() &&
		   "Cannot loop through manager entities if and version and entity vectors differ in size");
	assert(next_entity_ <= entities_.size() &&
		   "Last entity must be within entity vector range");
	auto pools{
			std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...) };
	// Check that none of the requested component pools are null.
	bool manager_has_components{
		((std::get<impl::Pool<TComponents>*>(pools)) && ...) };
	if (manager_has_components) {
		// Cycle through all manager entities.
		for (impl::Id entity{ 0 }; entity < next_entity_; ++entity) {
			// If entity is alive, call lambda on it.
			if (entities_[entity]) {
				bool doesnt_have_components{
					(!std::get<impl::Pool<TComponents>*>(pools)->impl::Pool<TComponents>::Has(entity) || ...) };
				if (doesnt_have_components) {
					function(Entity{ entity, versions_[entity], this });
				}

			}
		}
	}
}

// Entity comparison with null entity.

inline bool operator==(const Entity& entity, const impl::NullEntity& null_entity) {
	return null_entity == entity;
}

inline bool operator!=(const Entity& entity, const impl::NullEntity& null_entity) {
	return !(null_entity == entity);
}

} // namespace ecs

namespace std {

// Custom hashing function for ecs::Entity class.
// This allows for use of unordered maps and sets with entities as keys.
template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& e) const {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t hash{ 17 };
		hash = hash * 31 + std::hash<ecs::Manager*>()(e.manager_);
		hash = hash * 31 + std::hash<ecs::impl::Id>()(e.entity_);
		hash = hash * 31 + std::hash<ecs::impl::Version>()(e.version_);
		return hash;
	}
};

} // namespace std