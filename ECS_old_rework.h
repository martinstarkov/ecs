/*

MIT License

Copyright (c) 2020 | Martin Starkov | https://github.com/martinstarkov

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

#include <cstdlib> // std::size_t
#include <vector> // dynamic storage container for components / entities
#include <array> // fixed size container for component id caching during processes which call HasComponent and GetComponent
#include <cassert> // debug assertions
#include <atomic> // thread safe integers for component / manager id counter
#include <deque> // fast pop_front for free entity list
#include <iostream> // std::err for exception handling
#include <tuple> // tuples for storing different types of components in cached entity vectors
#include <functional> // std::hash for hashing
#include <type_traits> // std::is_base_of_v, std::enable_if, etc

// TODO: Check that GetDeadEntityCount and GetEntityCount functions work as intended.
// TODO: Check that RemoveComponents functions work as intended.
// TODO: Update documentation with GetDeadEntityCount, GetEntityCount, RemoveComponents.
// TODO: Check that documentation is fully up to date.
// TODO: Write tests for is_constructible.
// TODO: Write tests for ForEach.

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

// First valid entity for internal for-loops.
constexpr EntityId first_valid_entity_id = null + 1;

// Invalid version.
constexpr EntityVersion null_version = 0;
// Invalid manager id.
constexpr ManagerId null_manager_id = -1;
// Indicates a nonexistent component in a pool.
constexpr Offset INVALID_OFFSET = -1;

// Destructor pointer alias.
using Destructor = void (*)(void*);

// Useful type traits.

template <typename Class, typename ...Arguments>
using is_constructible = std::enable_if_t<std::is_constructible_v<Class, Arguments...>, bool>;

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual void VirtualRemove(const EntityId entity) = 0;
};

// Object which allows for contiguous storage of components of a single type (with runtime addition!).
template <typename TComponent>
class Pool : public BasePool {
public:
	// Since the manager stores component pools by their component's id (for fast random access), a null component pool must exist to fill gaps inside the sparse vector. This, because one manager could have a component pool added which is out of range due to it existing in another manager (component ids are shared among managers).
	Pool() {
		Allocate(sizeof(TComponent));
	}
	// Free memory block and call destructors on every component.
	~Pool() {
		for (auto offset : offsets_) {
			if (offset != INVALID_OFFSET) {
				auto address = pool_ + offset;
				address->~TComponent();
			}
		}
		try {
			assert(pool_ != nullptr && "Cannot free invalid component pool pointer");
			std::free(pool_);
		} catch (std::exception& e) {
			// Throw exception if component pool memory could not be freed. For example, if the component pool destructor is entered for a pool in an invalid state, such as after vector emplace induced move operations).
			std::cerr << "Cannot free memory for a component pool in an invalid state: " << e.what() << std::endl;
			abort();
		}
		pool_ = nullptr;
		// Byte capacity of the pool.
		capacity_ = 0;
		// Byte size of the pool.
		size_ = 0;
		offsets_.clear();
		free_offsets_.clear();
	}
	// Component pools should never be copied or reassigned.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;
	// Move operator used for resizing a vector of component pools (in the manager class).
	Pool(Pool&& obj) noexcept : pool_{ obj.pool_ }, capacity_{ obj.capacity_ }, size_{ obj.size_ }, offsets_{ std::exchange(obj.offsets_, {}) }, free_offsets_{ std::exchange(obj.free_offsets_, {}) } {
		obj.pool_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	// Component pool move assignment is used when null pools are assigned a valid pool (upon addition of the first component of a unique type).
	Pool& operator=(Pool&& obj) noexcept {
		for (auto offset : offsets_) {
			if (offset != INVALID_OFFSET) {
				auto address = pool_ + offset;
				address->~TComponent();
			}
		}
		try {
			assert(pool_ != nullptr && "Cannot free invalid component pool pointer");
			std::free(pool_);
		} catch (std::exception& e) {
			// Throw exception if component pool memory could not be freed. For example, if the component pool destructor is entered for a pool in an invalid state, such as after vector emplace induced move operations).
			std::cerr << "Cannot free memory for a component pool in an invalid state: " << e.what() << std::endl;
			abort();
		}

		pool_ = obj.pool_;
		capacity_ = obj.capacity_;
		size_ = obj.size_;
		offsets_ = std::exchange(obj.offsets_, {});
		free_offsets_ = std::exchange(obj.free_offsets_, {});
		
		obj.pool_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;

		return *this;
	}
	virtual void VirtualRemove(const EntityId entity) override final {
		Remove(entity);
	}
	// Add a new component address to the pool and return a void pointer to its location.
	template <typename ...TArgs>
	TComponent* Add(const EntityId id, TArgs&&... args) {
		auto offset = GetOffset();
		if (id >= offsets_.size()) { // if the entity id exceeds the indexing table's size, expand the indexing table
			offsets_.resize(id + 1, INVALID_OFFSET);
		}
		offsets_[id] = offset;
		auto address = pool_ + offset;
		address->~TComponent();
		new(address) TComponent(std::forward<TArgs>(args)...);
		return address;
	}
	// Call the component's destructor and remove its address from the component pool.
	void Remove(const EntityId id) {
		if (id < offsets_.size()) {
			auto& offset = offsets_[id];
			if (offset != INVALID_OFFSET) {
				auto address = pool_ + offset;
				address->~TComponent();
				free_offsets_.emplace_back(offset);
				offset = INVALID_OFFSET;
			}
		}
	}
	// Retrieve the memory location of a component, or nullptr if it does not exist
	TComponent* Get(const EntityId id) {
		if (Has(id)) {
			return pool_ + offsets_[id];
		}
		return nullptr;
	}
	// Check if the component pool contains a valid component offset for a given entity id.
	bool Has(const EntityId id) const {
		return id < offsets_.size() && offsets_[id] != INVALID_OFFSET;
	}
private:
	// Initial pool memory allocation, should only called once in non-empty pool constructors.
	void Allocate(const std::size_t starting_capacity) {
		assert(size_ == 0 && capacity_ == 0 && pool_ == nullptr && "Cannot call initial memory allocation for occupied component pool");
		capacity_ = starting_capacity;
		TComponent* memory = nullptr;
		try {
			memory = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		} catch (std::exception& e) {
			// Could not allocate enough memory for component pool (malloc failed).
			std::cerr << e.what() << std::endl;
			throw;
		}
		assert(memory != nullptr && "Failed to allocate initial memory for component pool");
		pool_ = memory;
	}
	// Double the size of a pool if the given capacity exceeds the previous capacity.
	void ReallocateIfNeeded(const std::size_t new_capacity) {
		if (new_capacity >= capacity_) {
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			capacity_ = new_capacity * 2; // Double the capacity.
			TComponent* memory = nullptr;
			try {
				memory = static_cast<TComponent*>(std::realloc(pool_, capacity_ * sizeof(TComponent)));
			} catch (std::exception& e) {
				// Could not reallocate enough memory for component pool (realloc failed).
				std::cerr << e.what() << std::endl;
				throw;
			}
			assert(memory != nullptr && "Failed to reallocate memory for pool");
			pool_ = memory;
		}
	}
	// Returns the first available component offset in the component pool.
	Offset GetOffset() {
		auto next_free_offset = INVALID_OFFSET;
		if (free_offsets_.size() > 0) {
			// Use first free offset found in the pool.
			next_free_offset = free_offsets_.front();
			// Remove the offset from free offsets.
			free_offsets_.pop_front();
		} else {
			// Use the end of the pool.
			next_free_offset = size_;
			++size_;
			// Expand the pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_free_offset != INVALID_OFFSET && "Could not find a valid component offset from component pool");
		return next_free_offset;
	}
	// Pointer to the beginning of the component pool's memory block.
	TComponent* pool_{ nullptr };
	// Byte capacity of the pool.
	std::size_t capacity_{ 0 };
	// Byte size of the pool.
	std::size_t size_{ 0 };
	std::vector<Offset> offsets_;
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
	bool marked = false;
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
	virtual void SetComponentDependencies() = 0;
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
	void Refresh() {}
	// Clear the manager, this will destroy all entities and components in the memory of the manager.
	void Clear() {
		entity_count_ = 0;
		// Keep the first 'null' entity.
		entities_.resize(1);
		entities_.shrink_to_fit();
		for (auto pool : pools_) {
			delete pool;
			pool = nullptr;
		}
		pools_.clear();
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
	// Retrieve a vector of handles to each entity in the manager.
	std::vector<Entity> GetEntities();
private:
	void DestroyEntity(const EntityId id, const EntityVersion version, bool loop_entity) {
		if (IsAlive(id, version)) {
			RemoveComponents(id, loop_entity);
			// Increment entity version, this will invalidate all entity handles with the previous version
			assert(id < entities_.size() && "Could not increment dead entity id");
			++entities_[id].version;
			entities_[id].alive = false;
			free_entity_ids.emplace_back(id);
		}
	}
	// Retrieve a reference to the component pool that matches the component id, expand is necessary.
	template <typename TComponent>
	internal::Pool<TComponent>* GetPool(const ComponentId component) const {
		assert(component < pools_.size());
		return static_cast<internal::Pool<TComponent>*>(pools_[component]);
	}
	// Check if a system id exists in the manager.
	bool IsValidSystem(const SystemId id) const {
		return id < systems_.size() && systems_[id];
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
		const auto pool = GetPool<T>(component_id);
		assert(pool != nullptr);
		auto c = pool->Get(id);
		assert(c != nullptr);
		return *c;
	}
	
	void RemoveComponents(const EntityId id, bool loop_entity) {
		for (auto pool : pools_) {
			if (pool) {
				pool->VirtualRemove(id);
				//ComponentChange(id, i, loop_entity);
			}
		}
	}

	template <typename TComponent>
	bool HasComponent(const EntityId entity) const {
		auto component = GetComponentId<TComponent>();
		const auto pool = GetPool<TComponent>(component);
		return pool != nullptr && pool->Has(entity);
	}
	
	template <typename T, typename ...TArgs>
	T& AddComponent(const EntityId id, bool loop_entity, TArgs&&... args) {
		static_assert(std::is_constructible_v<T, TArgs...>, "Cannot add component with given constructor argument list");
		static_assert(std::is_destructible_v<T>, "Cannot add component without valid destructor");
		auto component = GetComponentId<T>();
		if (component >= pools_.size()) {
			pools_.resize(component + 1);
		}
		auto pool = GetPool<T>(component);
		bool new_component = pool == nullptr;
		if (new_component) {
			pool = new internal::Pool<T>();
			pools_[component] = pool;
		}
		assert(pool != nullptr);
		return *pool->Add(id, std::forward<TArgs>(args)...);
	}
	template <typename T>
	void RemoveComponent(const EntityId id, bool loop_entity) {
		auto component = GetComponentId<T>();
		auto pool = GetPool<T>(component);
		assert(pool != nullptr);
		pool->Remove(id);
	}
	// Double the entity id vector if capacity is reached.
	void GrowEntitiesIfNeeded(const EntityId id) {
		if (id >= entities_.size()) {
			auto capacity = entities_.capacity() * 2;
			assert(capacity != 0 && "Capacity is 0, cannot double size of entities_ vector");
			entities_.resize(capacity);
		}
	}
	// Create / retrieve a unique id for each component class.
	template <typename T>
	static ComponentId GetComponentId() {
		static ComponentId id = ComponentCount()++;
		return id;
	}
	// Create / retrieve a unique id for each system class.
	template <typename T>
	static SystemId GetSystemId() {
		static SystemId id = SystemCount()++;
		return id;
	}
	// Total entity count (dead / invalid and alive).
	EntityId entity_count_{ 0 };
	// Unique manager identifier (used to compare managers).
	ManagerId id_{ internal::null_manager_id };
	// Dense vector of entity ids that map to specific metadata (version and dead/alive).
	std::vector<internal::EntityData> entities_;
	// Sparse vector of component pools.
	mutable std::vector<internal::BasePool*> pools_;
	// Free list of entity ids to be used before incrementing count.
	std::deque<EntityId> free_entity_ids;
	// Sparse vector of system pointers.
	std::vector<std::unique_ptr<BaseSystem>> systems_;
	// Used for generating unique manager ids.
	static ManagerId& ManagerCount() { static ManagerId id{ internal::null_manager_id }; return id; }
	// Important design decision: Component ids shared among all created managers, i.e. struct Position has id '3' in all manager instances, as opposed to the order in which a component is first added to each manager.
	static ComponentId& ComponentCount() { static ComponentId id{ 0 }; return id; }
	// Similarly to component ids, system ids are shared for all managers.
	static SystemId& SystemCount() { static SystemId id{ 0 }; return id; }

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
	// Make sure to also call this destructor when potentially overriding it.
	virtual ~System() override {
		entities.~vector();
		component_bitset_.~vector();
		manager_ = nullptr;
	}
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
	void SetComponentDependencies() override final {
		(AddComponentDependency<Cs>(), ...);
	}
	// Add a template argument into a dynamic component bitset so system dependency on a component can be checked.
	template <typename C>
	void AddComponentDependency() {
		ComponentId component_id = Manager::GetComponentId<C>();
		if (component_id >= component_bitset_.size()) {
			component_bitset_.resize(static_cast<std::size_t>(component_id) + 1, false);
		}
		component_bitset_[component_id] = true;
	}
	// A dynamic bitset of components which the system requires
	std::vector<bool> component_bitset_;
	Manager* manager_ = nullptr;
	bool cache_refresh_required_ = false;
};

// ECS Entity Handle
class Entity {
public:
	// Default construction to null entity.
	Entity(EntityId id = null) : id_{ id }, version_{ internal::null_version }, manager_{ nullptr }, loop_entity_{ false } {}
	// Entity construction within the manager.
	Entity(EntityId id, EntityVersion version, Manager* manager, bool loop_entity = false) : id_{ id }, version_{ version }, manager_{ manager }, loop_entity_{ loop_entity } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	friend class Manager;
	
	// Add a component to the entity, returns a reference to the new component.
	template <typename T, typename ...TArgs>
	T& AddComponent(TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<T, TArgs...>, "Cannot add component with given constructor argument list");
		static_assert(std::is_destructible_v<T>, "Cannot add component without valid destructor");
		return manager_->AddComponent<T>(id_, loop_entity_, std::forward<TArgs>(constructor_args)...);
	}
	// Remove a component from the entity.
	template <typename T>
	void RemoveComponent() {
		return manager_->RemoveComponent<T>(id_, loop_entity_);
	}
	// Remove all components from the entity.
	void RemoveComponents() {
		return manager_->RemoveComponents(id_, loop_entity_);
	}
	template <typename T>
	T& GetComponent() {
		return manager_->GetComponent<T>(id_, manager_->GetComponentId<T>());
	}
	// Returns whether or not the entity is alive
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(id_, version_);
	}
	void Destroy() {
		if (IsAlive()) {
			manager_->DestroyEntity(id_, version_, loop_entity_);
		}
	}
	template <typename TComponent>
	bool HasComponent() {
		assert(IsAlive());
		return manager_->HasComponent<TComponent>(id_);
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
inline std::vector<Entity> Manager::GetEntities() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	// Cycle through all manager entities.
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		entities.emplace_back(id, entity_data.version, this);
	}
	return entities;
}

template <typename ...Cs>
inline void System<Cs...>::ResetCache() {
	//entities = manager_->GetComponentTuple<Cs...>();
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