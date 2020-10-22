#pragma once

#include <cstdlib> // std::size_t
#include <vector> // dynamic storage container for components / entities
#include <array> // fixed size container for component id caching during processes which call HasComponent and GetComponent
#include <cassert> // debug assertions
#include <atomic> // thread safe integers for component / manager id counter
#include <deque> // fast pop_front for free entity list
#include <iostream> // std::err for exception handling
#include <tuple> // tuples for storing different types of components in cached entity vectors
#include <functional> // std::hash for hashing

namespace ecs {

using EntityId = std::uint32_t;
using EntityVersion = std::uint32_t;
using ComponentId = std::uint32_t;
using AtomicComponentId = std::atomic_uint32_t;
using SystemId = std::uint32_t;
using AtomicSystemId = std::atomic_uint32_t;
using ManagerId = std::int32_t;
using Offset = std::int64_t;

// Constants
// Null entity for comparison operations
constexpr EntityId null = 0;

namespace internal {

// First valid entity for internal for-loops
constexpr EntityId first_valid_entity_id = null + 1;

// Invalid version
constexpr EntityVersion null_version = 0;
// Invalid manager id
constexpr ManagerId null_manager_id = -1;
// Indicates a nonexistent component in a pool
constexpr Offset INVALID_OFFSET = -1;

// Important design decision: Component ids shared among all created managers, i.e. struct Position has id '3' in all manager instances, as opposed to the order in which a component is first added to each manager.
static ComponentId component_counter{ 0 };
// Similarly to component ids, system ids are shared for all managers.
static SystemId system_counter{ 0 };

// Create / retrieve a unique id for each component class.
template <typename T>
static ComponentId GetComponentId() {
	static ComponentId id = component_counter++;
	return id;
}

// Create / retrieve a unique id for each system class.
template <typename T>
static SystemId GetSystemId() {
	static SystemId id = system_counter++;
	return id;
}

// Store component destructor pointers for freeing pool memory.
template <typename T>
static void DestroyComponent(void* component_address) {
	static_cast<T*>(component_address)->~T();
}
// Destructor pointer alias
using Destructor = void (*)(void*);

// Object which allows for contiguous storage of components of a single type (with runtime addition!).
class ComponentPool {
public:
	// Since the manager stores component pools by their component's id (for fast random access), a null component pool must exist to fill gaps inside the sparse vector. This, because one manager could have a component pool added which is out of range due to it existing in another manager (component ids are shared among managers).
	ComponentPool() = default;
	// Non-empty pool constructor.
	ComponentPool(std::size_t component_size, Destructor destructor) : component_size_{ component_size }, destructor_{ destructor } {
		// Allocate enough memory for one component to begin with (not two because some components such as 'Player' might only ever appear once).
		AllocatePool(component_size);
	}
	// Free memory block and call destructors on every component.
	~ComponentPool() {
		if (pool_ != nullptr) { // Free memory block if destructor was not called by move operation (which sets pool_ to nullptr).
			assert(destructor_ != nullptr && "Cannot call invalid destructor on the components in a component pool");
			for (auto offset : component_offsets_) {
				if (offset != INVALID_OFFSET) { // Ignore nonexistent components.
					destructor_(static_cast<void*>(pool_ + offset)); // Call destructor on the component's memory offset from the beginning of the pool.
				}
			}
			// Free component pool memory block
			try {
				assert(pool_ != nullptr && "Cannot free invalid component pool pointer");
				free(static_cast<void*>(pool_));
			} catch (std::exception& e) {
				// Throw exception if component pool memory could not be freed. For example, if the component pool destructor is entered for a pool in an invalid state, such as after vector emplace induced move operations).
				std::cerr << "Cannot free memory for a component pool in an invalid state: " << e.what() << std::endl;
				abort();
			}
			// Invalidate pool state so destructor.
			pool_ = nullptr;
		}
		// State invalidation (setting everything to 0) happens for both regular destructor usage and when a component pool is moved.
		destructor_ = nullptr;
		capacity_ = 0;
		size_ = 0;
		component_size_ = 0;
		component_offsets_.clear();
		free_offsets_.clear();
	}
	// Component pools should never be copied or reassigned.
	ComponentPool(const ComponentPool&) = delete;
	ComponentPool& operator=(const ComponentPool&) = delete;
	// Move operator used for resizing a vector of component pools (in the manager class).
	ComponentPool(ComponentPool&& obj) noexcept : pool_{ obj.pool_ }, destructor_{ obj.destructor_ }, capacity_{ obj.capacity_ }, size_{ obj.size_ }, component_size_{ obj.component_size_ }, component_offsets_{ std::move(obj.component_offsets_) }, free_offsets_{ std::move(free_offsets_) } {
		// This allows the component pool destructor to be called without freeing or reallocating pool memory.
		obj.pool_ = nullptr;
	}
	// Component pool move assignment is used when null pools are assigned a valid pool (upon addition of the first component of a unique type).
	ComponentPool& operator=(ComponentPool&& obj) noexcept {
		pool_ = obj.pool_;
		destructor_ = obj.destructor_;
		capacity_ = obj.capacity_;
		size_ = obj.size_;
		component_size_ = obj.component_size_;
		component_offsets_ = std::move(obj.component_offsets_);
		free_offsets_ = std::move(free_offsets_);
		obj.pool_ = nullptr;
		obj.destructor_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
		obj.component_size_ = 0;
		obj.component_offsets_.clear();
		obj.free_offsets_.clear();
		return *this;
	}
	// Add a new component address to the pool and return a void pointer to its location.
	void* AddComponentAddress(const EntityId id) {
		assert(IsValid() && "Cannot add component address to null pool");
		assert(!HasComponentAddress(id) && "Cannot add new component address when it already exists in the component pool");
		Offset component_offset = GetAvailableComponentOffset();
		AddComponentOffset(id, component_offset);
		assert(pool_ != nullptr && "Could not add component address to a null pool");
		return static_cast<void*>(pool_ + component_offset);
	}
	// Call the component's destructor and remove its address from the component pool.
	void RemoveComponentAddress(const EntityId id) {
		if (IsValid() && id < component_offsets_.size()) { // Pools exists and entity id exists in indexing table.
			auto& component_offset = component_offsets_[id];
			if (component_offset != INVALID_OFFSET) { // The component exists in the pool.
				assert(pool_ != nullptr && "Could not remove component address from a null pool");
				void* component_address = static_cast<void*>(pool_ + component_offset);
				 // Destroy component at address.
				destructor_(component_address);
				// CONSIDER / TODO: Consider the implications of setting memory to zero in nested for loops, where component references could be invalidated after being set.
				// Set removed component's memory address to 0 (freed later, in the component pool's destructor as the offset can be reused).
				//std::memset(component_address, 0, component_size_);
				// Recycle the component's memory address offset.
				free_offsets_.emplace_back(component_offset);
				// Invalidate component offset in the indexing table (entity no longer has the component).
				component_offset = INVALID_OFFSET;
			}
		}
	}
	// Retrieve the memory location of a component, or nullptr if it does not exist
	void* GetComponentAddress(const EntityId id) const {
		if (IsValid() && id < component_offsets_.size()) { // Pool is valid and entity id exists in indexing table.
			auto component_offset = component_offsets_[id];
			if (component_offset != INVALID_OFFSET) { // The component exists in the pool.
				assert(pool_ != nullptr && "Cannot get a component address from a null component pool");
				return static_cast<void*>(pool_ + component_offset);
			}
		}
		return nullptr;
	}
	// Check if the component pool contains a valid component offset for a given entity id.
	bool HasComponentAddress(const EntityId id) const {
		return IsValid() && id < component_offsets_.size() && component_offsets_[id] != INVALID_OFFSET;
	}
	// Determine whether or not the component pool is a null pool (used for non-existent components to maintain the sparse component pool holes).
	bool IsValid() const {
		return pool_ != nullptr;
	}
	// Returns a destructor to the pool's components, used for replacing components
	Destructor GetDestructor() const {
		return destructor_;
	}
private:
	// Initial pool memory allocation, should only called once in non-empty pool constructors.
	void AllocatePool(const std::size_t starting_capacity) {
		assert(size_ == 0 && capacity_ == 0 && pool_ == nullptr && "Cannot call initial memory allocation for occupied component pool");
		capacity_ = starting_capacity;
		void* memory = nullptr;
		try {
			memory = malloc(capacity_);
		} catch (std::exception& e) {
			// Could not allocate enough memory for component pool (malloc failed).
			std::cerr << e.what() << std::endl;
			throw;
		}
		assert(memory != nullptr && "Failed to allocate initial memory for component pool");
		pool_ = static_cast<char*>(memory);
	}
	// Double the size of a pool if the given capacity exceeds the previous capacity.
	void ReallocatePoolIfNeeded(const std::size_t new_capacity) {
		if (new_capacity >= capacity_) {
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			capacity_ = new_capacity * 2; // Double the capacity.
			void* memory = nullptr;
			try {
				memory = realloc(pool_, capacity_);
			} catch (std::exception& e) {
				// Could not reallocate enough memory for component pool (realloc failed).
				std::cerr << e.what() << std::endl;
				throw;
			}
			assert(memory != nullptr && "Failed to reallocate memory for pool");
			pool_ = static_cast<char*>(memory);
		}
	}
	// Returns the first available component offset in the component pool.
	Offset GetAvailableComponentOffset() {
		auto next_free_offset = INVALID_OFFSET;
		if (free_offsets_.size() > 0) {
			// Use first free offset found in the pool.
			next_free_offset = free_offsets_.front();
			// Remove the offset from free offsets.
			free_offsets_.pop_front();
		} else {
			// Use the end of the pool.
			next_free_offset = size_;
			size_ += component_size_;
			// Expand the pool if necessary.
			ReallocatePoolIfNeeded(size_);
		}
		assert(next_free_offset != INVALID_OFFSET && "Could not find a valid component offset from component pool");
		return next_free_offset;
	}
	// Add an indexing table entry of the component's offset from the beginning of the component pool, indexed by entity id.
	void AddComponentOffset(const EntityId id, const Offset component_offset) {
		if (id >= component_offsets_.size()) { // if the entity id exceeds the indexing table's size, expand the indexing table
			const std::size_t new_size = id + 1;
			component_offsets_.resize(new_size, INVALID_OFFSET);
		}
		component_offsets_[id] = component_offset;
	}
	// Pointer to the beginning of the component pool's memory block.
	char* pool_{ nullptr };
	// Pointer to a wrapper around a component's destructor function call (destructor pointers don't exist in c++).
	Destructor destructor_{ nullptr };
	// Byte capacity of the pool.
	std::size_t capacity_{ 0 };
	// Byte size of the pool.
	std::size_t size_{ 0 };
	// The size each component stored in the component pool.
	std::size_t component_size_{ 0 };
	// Indexing table where the index is an entity id, the corresponding offset is the entity's component's offset from the beginning of the pool memory block.
	std::vector<Offset> component_offsets_;
	// Deque because pop_front is available and iteration / random access are not required (since the first free offset is always used).
	std::deque<Offset> free_offsets_;
};

// Holds the state of an entity id
struct EntityData {
	EntityData() = default;
	~EntityData() = default;
	EntityData& operator=(const EntityData&) = delete;
	EntityData& operator=(EntityData&&) = delete;
	EntityData(const EntityData& copy) noexcept : version{ copy.version }, alive{ copy.alive } {}
	EntityData(EntityData&& obj) noexcept : version{ obj.version }, alive{ obj.alive } {
		obj.version = null_version;
		obj.alive = false;
	}
	// Internal 'version' counter which invalidates all previous handles to an entity when it is destroyed
	EntityVersion version = null_version;
	// Determines if the entity id is currently in use (or invalidated / free)
	bool alive = false;
};

} // namespace internal

class Entity;
class Manager;

class BaseSystem { // System template class interface for homogenous storage of system pointers
public:
	virtual void Init(Manager* manager) = 0;
	virtual void Update() = 0;
	virtual Manager& GetManager() = 0;
	virtual bool DependsOn(ComponentId component_id) const = 0;
	virtual void ResetCache() = 0;
	virtual bool GetCacheRefreshRequired() const = 0;
	virtual void SetCacheRefreshRequired(bool required) = 0;
	virtual ~BaseSystem() {}
};

// Component and entity registry for the ECS.
class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_ (null entity's index).
	Manager() : entities_{ internal::EntityData{} }, id_{ ++ManagerCount() } {}
	// Invalidate manager id, reset entity count, and call destructors on everything.
	~Manager() {
		id_ = internal::null_manager_id;
		entity_count_ = 0;
		entities_.~vector();
		pools_.~vector();
		systems_.~vector();
		free_entity_ids.~deque();
	}
	// Destroy all entities in the manager.
	void Clear() {
		entity_count_ = 0;
		// Keep the first 'null' entity.
		entities_.resize(1);
		entities_.shrink_to_fit();
		pools_.resize(0);
		pools_.shrink_to_fit();
		free_entity_ids.resize(0);
		free_entity_ids.shrink_to_fit();
		// Let systems know their caches are invalid.
		for (auto& system : systems_) {
			if (system) {
				system->SetCacheRefreshRequired(true);
			}
		}
	}
	// Managers should not be copied.
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&& obj) noexcept : id_{ obj.id_ }, entity_count_{ obj.entity_count_ }, entities_{ std::move(obj.entities_) }, pools_{ std::move(obj.pools_) } {}
	Manager& operator=(Manager&& obj) = delete;

	// Manager comparison operators

	friend inline bool operator==(const Manager& lhs, const Manager& rhs) {
		return lhs.id_ == rhs.id_;
	}
	friend inline bool operator!=(const Manager& lhs, const Manager& rhs) {
		return lhs.id_ == rhs.id_;
	}

	friend class Entity;
	template <typename ...Ts>
	friend class System;
	// Add a new entity to the manager and return a handle to it.
	Entity CreateEntity();
	// Remove an entity from the manager.
	void DestroyEntity(Entity entity);
	// Add a system to the manager.
	template <typename T>
	void AddSystem();
	// Update a specific system in the manager.
	template <typename T>
	void Update();
	// Cycle through every single entity in the manager.
	// Populates the lambda function's parameter list with a handle to the current entity.
	template <typename T>
	void ForEachEntity(T&& function);
	// Cycle through each entity in the manager which has all of the given components.
	// Populates the lambda function's parameter list with a handle to the current entity and a reference to each component (in order of template argument types).
	template <typename ...Ts, typename T>
	void ForEach(T&& function);
	// Return the number of entities which are currently alive in the manager.
	std::size_t GetEntityCount();
	// Retrieve a vector of handles to each entity in the manager.
	std::vector<Entity> GetEntities();
	// Retrieve a vector of handles to each entity in the manager which has all of the given components.
	template <typename ...Ts>
	std::vector<Entity> GetEntitiesWith();
	// Retrieve a vector of handles to each entity in the manager which does not have all of the given components.
	template <typename ...Ts>
	std::vector<Entity> GetEntitiesWithout();
	// Destroy all entities in the manager.
	void DestroyEntities() {
		Clear();
	}
	// Destroy entities which have all of the given components.
	template <typename ...Ts>
	void DestroyEntitiesWith() {
		auto entities = GetEntitiesWith<Ts...>();
		for (auto entity : entities) {
			entity.Destroy();
		}
	}
	// Destroy entities which do not have all of the given components.
	template <typename ...Ts>
	void DestroyEntitiesWithout() {
		auto entities = GetEntitiesWithout<Ts...>();
		for (auto entity : entities) {
			entity.Destroy();
		}
	}
	// Returns a vector of tuples where the first element is an entity and the rest are the requested components, only retrieves entities which have each component
	template <typename ...Ts>
	std::vector<std::tuple<Entity, Ts&...>> GetComponentTuple();
	// Add a component to an entity, returns a reference to the new component.
	template <typename T, typename ...TArgs>
	T& AddComponent(Entity entity, TArgs&&... constructor_args);
	// Replace a component of an entity, returns a reference to the new component.
	template <typename T, typename ...TArgs>
	T& ReplaceComponent(Entity entity, TArgs&&... constructor_args);
	// Remove a component from an entity.
	template <typename T>
	void RemoveComponent(Entity entity);
	// Remove multiple components from an entity.
	template <typename ...Ts>
	void RemoveComponents(Entity entity);
	// Returns a reference to a component.
	// Will throw if retrieving a nonexistent component (surround with HasComponent if uncertain).
	template <typename T>
	T& GetComponent(const Entity entity) const;
	// Returns a tuple of references to components.
	// Will throw if retrieving a nonexistent component (surround with HasComponents if uncertain).
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(const Entity entity) const;
	// Returns whether or not entity has a specific component.
	template <typename T>
	bool HasComponent(const Entity entity) const;
	// Returns whether or not entity has all listed components.
	template <typename ...Ts>
	bool HasComponents(const Entity entity) const;
	// Returns whether an entity is alive.
	bool IsAlive(const Entity entity) const;
private:
	// Internally used method implementations.

	// Event which is called upon component addition or removal from an entity.
	void ComponentChange(const EntityId id, const ComponentId component_id, bool loop_entity);
	// Add entity id to deletion list but do not invalidate any handles.
	void DestroyEntity(const EntityId id, const EntityVersion version, bool loop_entity) {
		if (IsAlive(id, version)) {
			for (std::size_t i = 0; i < pools_.size(); ++i) {
				auto& pool = GetPool(i);
				if (pool.IsValid()) {
					pool.RemoveComponentAddress(id);
					// Pool index in pools_ vector is the corresponding component's id.
					ComponentChange(id, i, loop_entity);
				}
			}
			// Increment entity version, this will invalidate all entity handles with the previous version
			assert(id < entities_.size() && "Could not increment dead entity id");
			++entities_[id].version;
			entities_[id].alive = false;
			free_entity_ids.emplace_back(id);
		}
	}
	// Call a lambda function and populate its parameter list with a reference to each requested component.
	template <typename ...Ts, typename T, std::size_t... component_id>
	void ForEachHelper(T&& function, const EntityId id, std::array<ComponentId, sizeof...(Ts)>& component_ids, std::index_sequence<component_id...>);
	// Wrapper for creating an index sequence in order to access each element of the array of component ids.
	template <typename ...Ts, typename T>
	void ForEachInvoke(T&& function, const EntityId id, std::array<ComponentId, sizeof...(Ts)>& component_ids) {
		ForEachHelper<Ts...>(std::forward<T>(function), id, component_ids, std::make_index_sequence<sizeof...(Ts)>{});
	}
	// Implementation of GetComponentTuple.
	template <std::size_t COMPONENT_COUNT, typename ...Ts, std::size_t... Is>
	void GetComponentTupleInvoke(std::vector<std::tuple<Entity, Ts&...>>& vector, const EntityId id, const EntityVersion version, const std::array<ComponentId, COMPONENT_COUNT>& component_ids, std::index_sequence<Is...>) {
		std::array<void*, COMPONENT_COUNT> component_locations;
		// Check if an entity has each component and if so, populate the component locations array.
		for (std::size_t i = 0; i < component_ids.size(); ++i) {
			void* component_location = GetComponentLocation(id, component_ids[i]);
			if (!component_location) return; // Component does not exist, do not add entity / components to tuple, return.
			component_locations[i] = component_location;
		}
		// Expand and format the component locations array into a tuple and add it to the vector of tuples.
		vector.emplace_back(Entity{ id, version, this, true }, (*static_cast<Ts*>(component_locations[Is]))...);
	}
	// Retrieve a reference to the component pool that matches the component id, expand is necessary.
	internal::ComponentPool& GetPool(const ComponentId component_id) const {
		if (component_id >= pools_.size()) { // Expand pool vector if component id cannot be used (sparse vector).
			std::size_t new_size = component_id + 1;
			pools_.resize(new_size); // Resize calls null component pool constructor.
		}
		return pools_[component_id];
	}
	// Create a component pool for the given component id if it does not exist yet in the manger.
	template <typename T>
	internal::ComponentPool& AddPool(const ComponentId component_id) {
		auto& pool = GetPool(component_id);
		if (!pool.IsValid()) {
			// Add the component pool to the manager.
			pool = std::move(internal::ComponentPool{ sizeof(T), &internal::DestroyComponent<T> });
		}
		assert(pool.IsValid() && "Could not find or create a valid component pool for the component");
		return pool;
	}
	// Check if a system id exists in the manager.
	bool IsValidSystem(const SystemId id) const {
		return id < systems_.size() && systems_[id] != nullptr;
	}
	// Check if an entity id exists in the manager.
	bool IsValidEntity(const EntityId id) const {
		return id != null && id < entities_.size() && entities_[id].version != internal::null_version;
	}
	// Check if a given entity id is considered alive.
	bool IsAlive(const EntityId id, const EntityVersion version) const {
		return IsValidEntity(id) && entities_[id].alive && entities_[id].version == version;
	}
	// GetComponent implementation.
	template <typename T>
	T& GetComponent(const EntityId id, const ComponentId component_id) const {
		assert(IsValidEntity(id) && "Cannot get component from an invalid entity");
		const auto& pool = GetPool(component_id);
		void* component_location = pool.GetComponentAddress(id);
		assert(component_location != nullptr && "Cannot get component which does not exist");
		return *static_cast<T*>(component_location);
	}
	// Used for faster internal checking of HasComponent and then GetComponent (For example, in ForEach loops, check if component pointer is valid, if it is, use it).
	void* GetComponentLocation(EntityId id, ComponentId component_id) const {
		assert(IsValidEntity(id) && "Cannot get component from invalid entity");
		const auto& pool = GetPool(component_id);
		return pool.GetComponentAddress(id);
	}
	// GetComponent without passing a component id.
	template <typename T>
	T& GetComponent(const EntityId id) const {
		return GetComponent<T>(id, internal::GetComponentId<T>());
	}
	// GetComponents without passing a component id.
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(const EntityId id) const {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id)...);
	}
	// RemoveComponents implementation.
	template <typename ...Ts>
	void RemoveComponents(const EntityId id, bool loop_entity) {
		(RemoveComponent<Ts>(id, loop_entity), ...);
	}
	// HasComponent implementation.
	bool HasComponent(const EntityId id, const ComponentId component_id) const {
		assert(IsValidEntity(id) && "Cannot check if invalid entity has component");
		return component_id < pools_.size() && GetPool(component_id).HasComponentAddress(id);
	}
	// HasComponent without passing a component id. 
	template <typename T>
	bool HasComponent(const EntityId id) const {
		return HasComponent(id, internal::GetComponentId<T>());
	}
	// HasComponents without passing component ids.
	template <typename ...Ts>
	bool HasComponents(const EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}
	// HasComponents implementation.
	template <std::size_t I>
	bool HasComponents(const EntityId id, const std::array<ComponentId, I>& component_ids) const {
		assert(IsValidEntity(id) && "Cannot check if invalid entity has components");
		for (const auto component_id : component_ids) {
			if (!HasComponent(id, component_id)) return false;
		}
		return true;
	}
	// AddComponent implementation.
	template <typename T, typename ...TArgs>
	T& AddComponent(const EntityId id, bool loop_entity, TArgs&&... args) {
		assert(IsValidEntity(id) && "Cannot add component to invalid entity");
		auto component_id = internal::GetComponentId<T>();
		void* component_location = nullptr;
		auto& component_pool = AddPool<T>(component_id);
		component_location = component_pool.GetComponentAddress(id);
		bool new_component = false;
		if (!component_location) { // If component doesn't exist, add it.
			component_location = component_pool.AddComponentAddress(id);
			new_component = true;
		} else { // If component exists, replace it.
			internal::Destructor destructor = component_pool.GetDestructor();
			// Destroy the previous component.
			destructor(component_location);
		}
		assert(component_location != nullptr && "Could not add component address");
		new(component_location) T(std::forward<TArgs>(args)...);
		if (new_component) { // Small design decision: Don't trigger ComponentChange event when a component is replaced, as this does not invalidate any references so system caches don't have to be refreshed.
			ComponentChange(id, component_id, loop_entity);
		}
		return *static_cast<T*>(component_location);
	}
	// ReplaceComponent implementation.
	template <typename T, typename ...TArgs>
	T& ReplaceComponent(const EntityId id, TArgs&&... args) {
		assert(IsValidEntity(id) && "Cannot replace component of an invalid entity");
		assert(HasComponent<T>(id) && "Cannot replace a nonexistent component");
		return AddComponent<T>(id, false, std::forward<TArgs>(args)...);
	}
	// RemoveComponent implementation.
	template <typename T>
	void RemoveComponent(const EntityId id, bool loop_entity) {
		assert(IsValidEntity(id) && "Cannot remove component from invalid entity");
		auto component_id = internal::GetComponentId<T>();
		if (HasComponent(id, component_id)) {
			auto& pool = GetPool(component_id);
			pool.RemoveComponentAddress(id);
			ComponentChange(id, component_id, loop_entity);
		}
	}
	// Double the entity id vector if capacity is reached.
	void GrowEntitiesIfNeeded(const EntityId id) {
		if (id >= entities_.size()) {
			auto capacity = entities_.capacity() * 2;
			assert(capacity != 0 && "Capacity is 0, cannot double size of entities_ vector");
			entities_.resize(capacity);
		}
	}
	// Total entity count (dead / invalid and alive).
	EntityId entity_count_{ 0 };
	// Unique manager identifier (used to compare managers).
	ManagerId id_{ internal::null_manager_id };
	// Dense vector of entity ids that map to specific metadata (version and dead/alive).
	std::vector<internal::EntityData> entities_;
	// Sparse vector of component pools.
	mutable std::vector<internal::ComponentPool> pools_;
	// Free list of entity ids to be used before incrementing count.
	std::deque<EntityId> free_entity_ids;
	// Sparse vector of system pointers.
	std::vector<std::unique_ptr<BaseSystem>> systems_;
	// Used for generating unique manager ids.
	static ManagerId& ManagerCount() { static ManagerId id{ internal::null_manager_id }; return id; }

};

// Template class for ECS systems, template arguments are components which the system requires of its entities.
// Each system has access to a protected 'entities' variable.
// Example:
// struct MySystem : public System<Transform, RigidBody>;
// 'entities' can be used inside the system's methods like so:
// auto [auto first_entity, transform, rigid_body] = entities[0];
// or using a loop:
// for (auto [entity, transform, rigid_body] : entities) {
//	... use handle / component references here ... }
// If a system-required component is removed from an entity, that entity will be removed from 'entities'.
template <typename ...Cs>
class System : public BaseSystem {
public:
	System() : manager_{ nullptr }, cache_refresh_required_{ false } {
		(AddComponentDependency<Cs>(), ...);
	}
	friend class Manager;
protected:
	// A vector of tuples where the first tuple element is a copy of an entity handle and the rest are references to that entity's components as determined by the system's required components.
	// Entities are cached in the system which allows for fast traversal.
	std::vector<std::tuple<Entity, Cs&...>> entities;
	// Retrieve the manager, for possible use in system methods
	virtual Manager& GetManager() override final {
		assert(manager_ != nullptr && "Cannot get manager as system has not been properly initialized");
		return *manager_;
	}
	virtual ~System() override {}
private:
	// Add manager and call initial cache reset.
	virtual void Init(Manager* manager) override final {
		manager_ = manager;
		assert(manager_ != nullptr && "Could not initialize system with invalid manager");
		ResetCache();
	}
	// Refetch tuples of entity handles and components which match the system's required components. Called in the system constructor and when one of the system's required components is added or removed from an entity in the manager.
	virtual void ResetCache() override final;
	// Return whether or not a cache refresh is required after updating the system
	virtual bool GetCacheRefreshRequired() const override final {
		return cache_refresh_required_;
	}
	// Tells the system that once the update function is called, it must refresh its 'entities' variable (cache update).
	virtual void SetCacheRefreshRequired(bool required) override final {
		cache_refresh_required_ = required;
	}
	// Check if a component id is required by the system
	virtual bool DependsOn(ComponentId component_id) const override final {
		return component_id < component_bitset_.size() && component_bitset_[component_id];
	}
	// Add a template argument into a dynamic component bitset so system dependency on a component can be checked.
	template <typename C>
	void AddComponentDependency() {
		ComponentId component_id = internal::GetComponentId<C>();
		if (component_id >= component_bitset_.size()) {
			component_bitset_.resize(component_id + 1, false);
		}
		component_bitset_[component_id] = true;
	}
	// A dynamic bitset of components which the system requires
	std::vector<bool> component_bitset_;
	Manager* manager_;
	bool cache_refresh_required_;
};

// ECS Entity Handle
class Entity {
public:
	// Null entity construction
	Entity(EntityId id = null) : id_{ id }, version_{ internal::null_version }, manager_{ nullptr }, loop_entity_{ false } {}
	// Entity construction within the manager
	Entity(EntityId id, EntityVersion version, Manager* manager, bool loop_entity = false) : id_{ id }, version_{ version }, manager_{ manager }, loop_entity_{ loop_entity } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	friend class Manager;
	// Returns the entity's unique id
	EntityId GetId() const {
		return id_;
	}
	// Returns the entity's version (incremented upon death)
	EntityVersion GetVersion() const {
		return version_;
	}
	// Returns a pointer to the entity's manager
	Manager* GetManager() const {
		return manager_;
	}
	// Returns whether or not the entity is alive
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(id_, version_);
	}
	// Returns whether or not the entity is valid (not null)
	bool IsValid() const {
		return manager_ != nullptr && manager_->IsValidEntity(id_);
	}
	// Add a component to the entity, returns a reference to the new component.
	template <typename T, typename ...TArgs>
	T& AddComponent(TArgs&&... constructor_args) {
		assert(IsValid() && "Cannot add component to null entity");
		assert(IsAlive() && "Cannot add component to dead entity");
		return manager_->AddComponent<T>(id_, loop_entity_, std::forward<TArgs>(constructor_args)...);
	}
	// Replace a component of the entity, returns a reference to the new component.
	template <typename T, typename ...TArgs>
	T& ReplaceComponent(TArgs&&... constructor_args) {
		assert(IsValid() && "Cannot replace component of null entity");
		assert(IsAlive() && "Cannot replace component of dead entity");
		return manager_->ReplaceComponent<T>(id_, std::forward<TArgs>(constructor_args)...);
	}
	// Remove a component from the entity.
	template <typename T>
	void RemoveComponent() {
		assert(IsValid() && "Cannot remove component from null entity");
		assert(IsAlive() && "Cannot remove component from dead entity");
		return manager_->RemoveComponent<T>(id_, loop_entity_);
	}
	// Remove multiple components from the entity.
	template <typename ...Ts>
	void RemoveComponents() {
		assert(IsValid() && "Cannot remove components from null entity");
		assert(IsAlive() && "Cannot remove components from dead entity");
		return manager_->RemoveComponents<Ts...>(id_, loop_entity_);
	}
	// Check if the entity has a component.
	template <typename T>
	bool HasComponent() {
		assert(IsValid() && "Cannot check if null entity has a component");
		assert(IsAlive() && "Cannot check if dead entity has a component");
		return manager_->HasComponent<T>(id_);
	}
	// Check if the entity has all the given components.
	template <typename ...Ts>
	bool HasComponents() {
		assert(IsValid() && "Cannot check if null entity has components");
		assert(IsAlive() && "Cannot check if dead entity has components");
		return manager_->HasComponents<Ts...>(id_);
	}
	// Returns a reference to a component.
	// Will throw if retrieving a nonexistent component (surround with HasComponent if uncertain).
	template <typename T>
	T& GetComponent() {
		assert(IsValid() && "Cannot get component from null entity");
		assert(IsAlive() && "Cannot get component from dead entity");
		return manager_->GetComponent<T>(id_);
	}
	// Returns a tuple of references to components.
	// Will throw if retrieving a nonexistent component (surround with HasComponents if uncertain).
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents() {
		assert(IsValid() && "Cannot get components from null entity");
		assert(IsAlive() && "Cannot get components from dead entity");
		return manager_->GetComponents<Ts...>(id_);
	}
	// Destroy / Kill / Murder an entity and refresh the appropriate manager (this will invalidate the entity handle).
	void Destroy() {
		if (IsAlive()) {
			manager_->DestroyEntity(id_, version_, loop_entity_);
		}
	}

	// Entity comparison operators, the duplication allows avoiding implicit conversion of ecs::null into an entity

	inline bool operator==(const Entity& other) const {
		return manager_ == other.manager_ && id_ == other.id_ && version_ == other.version_;
	}
	friend inline bool operator==(const Entity& lhs, const EntityId& rhs) {
		return lhs.id_ == rhs;
	}
	friend inline bool operator==(const EntityId& lhs, const Entity& rhs) {
		return lhs == rhs.id_;
	}
	friend inline bool operator!=(const Entity& lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
	friend inline bool operator!=(const EntityId& lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
	friend inline bool operator!=(const Entity& lhs, const EntityId& rhs) {
		return !(lhs == rhs);
	}
private:
	EntityId id_;
	EntityVersion version_;
	Manager* manager_;
	bool loop_entity_;
};

struct EntityComparator {
	bool operator()(const Entity& a, const Entity& b) const {
		return a.GetId() < b.GetId();
	}
};

inline Entity Manager::CreateEntity() {
	EntityId id = null;
	if (free_entity_ids.size() > 0) { // Pick id from free list before trying to increment entity counter.
		id = free_entity_ids.front();
		free_entity_ids.pop_front();
	} else {
		id = ++entity_count_;
	}
	assert(id != null && "Could not create entity due to lack of free entity ids");
	GrowEntitiesIfNeeded(id);
	assert(id < entities_.size() && "Entity id outside of range of entities vector");
	entities_[id].alive = true;
	return Entity{ id, ++entities_[id].version, this };
}
inline void Manager::DestroyEntity(Entity entity) {
	DestroyEntity(entity.id_, entity.version_, entity.loop_entity_);
}
inline std::size_t Manager::GetEntityCount() {
	std::size_t entities = 0;
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (entity_data.alive) {
			entities += 1;
		}
	}
	return entities;
}
inline std::vector<Entity> Manager::GetEntities() {
	return GetEntitiesWith<>();
}
template <typename ...Ts>
inline std::vector<Entity> Manager::GetEntitiesWith() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	// Store all the component ids in an array so they don't have to be fetched in every loop cycle.
	std::array<ComponentId, sizeof...(Ts)> component_ids = { internal::GetComponentId<Ts>()... };
	// Cycle through all manager entities.
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (HasComponents(id, component_ids) && entity_data.alive) {
			// If entity's components match the required ones and it is alive, add its handle to the vector.
			entities.emplace_back(id, entity_data.version, this);
		}
	}
	entities.shrink_to_fit();
	return entities;
}
template <typename ...Ts>
inline std::vector<Entity> Manager::GetEntitiesWithout() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	// Store all the component ids in an array so they don't have to be fetched in every loop cycle.
	std::array<ComponentId, sizeof...(Ts)> component_ids = { internal::GetComponentId<Ts>()... };
	// Cycle through all manager entities.
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (!HasComponents(id, component_ids) && entity_data.alive) {
			// If entity's components DO NOT MATCH the required ones and it is alive, add its handle to the vector.
			entities.emplace_back(id, entity_data.version, this);
		}
	}
	entities.shrink_to_fit();
	return entities;
}
template <typename ...Ts>
inline std::vector<std::tuple<Entity, Ts&...>> Manager::GetComponentTuple() {
	std::vector<std::tuple<Entity, Ts&...>> vector_of_tuples;
	vector_of_tuples.reserve(entity_count_);
	constexpr std::size_t COMPONENT_COUNT = sizeof...(Ts);
	// Store all the component ids in an array so they don't have to be fetched in every loop cycle.
	std::array<ComponentId, COMPONENT_COUNT> component_ids = { internal::GetComponentId<Ts>()... };
	// Cycle through all manager entities.
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		assert(id < entities_.size() && "Entity id out of range");
		auto& entity_data = entities_[id];
		if (entity_data.alive) {
			// If entity is alive, attempt to retrieve a tuple of components for it.
			GetComponentTupleInvoke<COMPONENT_COUNT, Ts...>(vector_of_tuples, id, entity_data.version, component_ids, std::make_index_sequence<sizeof...(Ts)>());
		}
	}
	vector_of_tuples.shrink_to_fit();
	return vector_of_tuples;
}
template <typename ...Ts, typename T, std::size_t... component_id>
inline void Manager::ForEachHelper(T&& function, const EntityId id, std::array<ComponentId, sizeof...(Ts)>& component_ids, std::index_sequence<component_id...>) {
	function(Entity{ id, entities_[id].version, this, true }, GetComponent<Ts>(id, component_ids[component_id])...);
}
template <typename ...Ts, typename T>
inline void Manager::ForEach(T&& function) {
	// TODO: Write some tests for lambda function parameters.
	std::vector<std::tuple<Entity, Ts&...>> entities = GetComponentTuple<Ts...>();
	for (std::tuple<Entity, Ts&...> tuple : entities) {
		// Call the lambda for each entity with its tuple
		std::apply(function, tuple);
	}
}
template <typename T>
inline void Manager::ForEachEntity(T&& function) {
	// TODO: Write some tests for lambda function parameters.
	auto entities = GetEntities();
	for (auto entity : entities) {
		// Call the lambda for each entity in the manager
		function(entity);
	}
}
template <typename T, typename ...TArgs>
inline T& Manager::AddComponent(Entity entity, TArgs&&... constructor_args) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot add component to dead entity");
	return AddComponent<T>(entity.id_, entity.loop_entity_, std::forward<TArgs>(constructor_args)...);
}
template <typename T, typename ...TArgs>
inline T& Manager::ReplaceComponent(Entity entity, TArgs&&... constructor_args) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot replace component of dead entity");
	return ReplaceComponent<T>(entity.id_, std::forward<TArgs>(constructor_args)...);
}
template <typename T>
inline void Manager::RemoveComponent(Entity entity) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot remove component from dead entity");
	RemoveComponent<T>(entity.id_, entity.loop_entity_);
}
template <typename ...Ts>
inline void Manager::RemoveComponents(Entity entity) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot remove components from dead entity");
	(RemoveComponent<Ts>(entity.id_, entity.loop_entity_), ...);
}
template <typename T>
inline T& Manager::GetComponent(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot get component from dead entity");
	return GetComponent<T>(entity.id_, internal::GetComponentId<T>());
}
template <typename ...Ts>
inline std::tuple<Ts&...> Manager::GetComponents(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot get components from dead entity");
	return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(entity.id_, internal::GetComponentId<Ts>())...);
}
template <typename T>
inline bool Manager::HasComponent(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot check if dead entity has a component");
	return HasComponent(entity.id_, internal::GetComponentId<T>());
}
template <typename ...Ts>
inline bool Manager::HasComponents(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot check if dead entity has components");
	return (HasComponent<Ts>(entity.id_) && ...);
}
inline bool Manager::IsAlive(Entity entity) const {
	return IsAlive(entity.id_, entity.version_);
}
inline void Manager::ComponentChange(const EntityId id, const ComponentId component_id, bool loop_entity) {
	for (auto& system : systems_) {
		if (system) {
			if (system->DependsOn(component_id)) {
				assert(IsValidEntity(id) && "Cannot trigger component change event for invalid entity");
				// Apparently ignore this comment... // If an entity is a loop entity (from system 'entites') do not invalidate the cache as the range-based for-loop iterators will be invalidated and this will lead to undefined behavior.
				system->SetCacheRefreshRequired(true);
			}
		}
	}
}
template <typename T>
inline void Manager::AddSystem() {
	static_assert(std::is_base_of_v<BaseSystem, T>, "Cannot add a system to the manager which does not derive from ecs::System");
	SystemId system_id = internal::GetSystemId<T>();
	if (system_id >= systems_.size()) {
		systems_.resize(system_id + 1);
	}
	auto& system = systems_[system_id];
	system = std::make_unique<T>();
	system->Init(this);
}
template <typename T>
inline void Manager::Update() {
	static_assert(std::is_base_of_v<BaseSystem, T>, "Cannot update a system which does not derive from ecs::System");
	SystemId system_id = internal::GetSystemId<T>();
	assert(IsValidSystem(system_id) && "Cannot update a system which does not exist in manager");
	auto& system = systems_[system_id];
	assert(system && system.get() != nullptr && "Invalid system pointer, check system creation");
	if (system->GetCacheRefreshRequired()) {
		system->ResetCache();
	}
	system->Update();
	if (system->GetCacheRefreshRequired()) {
		system->ResetCache();
	}
}
template <typename ...Cs>
inline void System<Cs...>::ResetCache() {
	entities = manager_->GetComponentTuple<Cs...>();
	SetCacheRefreshRequired(false);
	/*
	// Checking whether entity exists isn't needed as this function is only entered upon addition / removal of a NEW component, therefore entity could not have existed before / not existed before.
	auto entity_exists = std::find_if(entities.begin(), entities.end(), [id](const Components& t) {
		return std::get<Entity>(t).GetId() == id;
	});
	if (entity_exists != / == entities.end()) { // entity found / not found in cache
		// Do stuff to cache
	}
	*/
}

} // namespace ecs

namespace std {

// Custom hashing function for ecs::Entity class allows for use of unordered maps with entities as keys
template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& k) const {
		using std::size_t;
		using std::hash;

		// Compute individual hash values for first,
		// second and third and combine them using XOR
		// and bit shifting:

		return ((hash<ecs::EntityId>()(k.GetId())
				 ^ (hash<ecs::EntityVersion>()(k.GetVersion()) << 1)) >> 1)
			^ (hash<ecs::Manager*>()(k.GetManager()) << 1);
	}
};

} // namespace std