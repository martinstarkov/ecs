#pragma once

#pragma once

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>
#include <atomic>
#include <deque>
#include <iostream>
#include <memory>
#include <tuple>
#include <functional>
#include <type_traits>

namespace ecs {

using EntityId = std::uint64_t;
using EntityVersion = std::uint32_t;
using ComponentId = std::uint32_t;
using AtomicComponentId = std::atomic_uint32_t;
using SystemId = std::uint32_t;
using AtomicSystemId = std::atomic_uint32_t;
using ManagerId = std::int32_t;
using Offset = std::int64_t;

// Constants
constexpr EntityId null = 0;
constexpr EntityId first_valid_entity_id = null + 1;
constexpr EntityVersion null_version = 0;

namespace internal {

constexpr ManagerId null_manager_id = -1;

// Indicates a nonexistent component in a pool
constexpr Offset INVALID_OFFSET = -1;

// Global component ids for all managers
extern AtomicComponentId component_counter;
AtomicComponentId component_counter{ 0 };

// Global system ids for all managers
extern AtomicSystemId system_counter;
AtomicSystemId system_counter{ 0 };

// Unique id for each new component type
template <typename T>
static ComponentId GetComponentId() {
	static ComponentId id = component_counter++;
	return id;
}

// Unique id for each new system
template <typename T>
static SystemId GetSystemId() {
	static SystemId id = system_counter++;
	return id;
}

// Store component destructor pointers for freeing pool memory
template<typename T>
void DestroyComponent(void* component_address) {
	static_cast<T*>(component_address)->~T();
}
// Destructor pointer alias
using Destructor = void (*)(void*);

class ComponentPool {
public:
	ComponentPool() = default; // null pool
	ComponentPool(std::size_t component_size, Destructor destructor) : component_size_{ component_size }, destructor_{ destructor } {
		AllocatePool(component_size);
	}
	// Free memory block and call destructors on everything
	~ComponentPool() {
		if (pool_ != nullptr) { // Free memory block if not a move destructor call
			assert(destructor_ != nullptr && "Cannot call invalid destructor on pool components");
			for (auto offset : component_offsets_) {
				if (offset != INVALID_OFFSET) {
					destructor_(static_cast<void*>(pool_ + offset));
				}
			}
			// Free memory pool
			try {
				assert(pool_ != nullptr && "Cannot free invalid pool pointer");
				std::free(static_cast<void*>(pool_));
			} catch (std::exception& e) {
				std::cerr << e.what() << std::endl;
				abort();
			}
			// Invalidate pool state
			pool_ = nullptr;
		}
		// These are called both by regular destructor and move triggered destructor
		destructor_ = nullptr;
		capacity_ = 0;
		size_ = 0;
		component_size_ = 0;
		component_offsets_.clear();
		free_offsets_.clear();
	}
	// Component pools should not be copied or reassigned
	ComponentPool(const ComponentPool&) = delete;
	ComponentPool& operator=(const ComponentPool&) = delete;
	// Move operator used for vector of component pool resizing
	ComponentPool(ComponentPool&& obj) noexcept : pool_{ obj.pool_ }, destructor_{ obj.destructor_ }, capacity_{ obj.capacity_ }, size_{ obj.size_ }, component_size_{ obj.component_size_ }, component_offsets_{ std::move(obj.component_offsets_) }, free_offsets_{ std::move(free_offsets_) } {
		// This allows the obj destructor to be called without freeing the pool memory
		obj.pool_ = nullptr;
	}
	ComponentPool& operator=(ComponentPool&& obj) noexcept {
		pool_ = obj.pool_;
		destructor_ = obj.destructor_;
		capacity_ = obj.capacity_;
		size_ = obj.size_;
		component_size_ = obj.component_size_;
		component_offsets_ = std::move(obj.component_offsets_);
		free_offsets_ = std::move(free_offsets_);
		// This allows the obj destructor to be called without freeing the pool memory
		obj.pool_ = nullptr;
		obj.destructor_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
		obj.component_size_ = 0;
		obj.component_offsets_.clear();
		obj.free_offsets_.clear();
		return *this;
	}
	void* AddComponentAddress(EntityId id) {
		Offset component_offset = AddToPool(id);
		AddComponentOffset(id, component_offset);
		assert(pool_ != nullptr && "Could not add component address to null pool");
		return static_cast<void*>(pool_ + component_offset);
	}
	void RemoveComponentAddress(EntityId id) {
		if (id < component_offsets_.size()) { // Id exists in component offsets
			auto& component_offset = component_offsets_[id];
			if (component_offset != INVALID_OFFSET) { // Component exists
				assert(pool_ != nullptr && "Could not remove component address from null pool");
				void* component_address = static_cast<void*>(pool_ + component_offset);
				 // destroy component at address
				destructor_(component_address);
				// set memory to 0 (freed in component pool destructor)
				// TODO: Consider the implications of this in nested for loops
				//std::memset(component_address, 0, component_size_);
				free_offsets_.emplace_back(component_offset);
				// invalidate component offset
				component_offset = INVALID_OFFSET;
			}
		}
	}
	void* GetComponentAddress(EntityId id) const {
		if (id < component_offsets_.size()) { // Entity id exists in component offsets
			auto component_offset = component_offsets_[id];
			if (component_offset != INVALID_OFFSET) { // Component exists in pool
				assert(pool_ != nullptr && "Cannot get a component address from a null pool");
				return static_cast<void*>(pool_ + component_offset);
			}
		}
		return nullptr;
	}
	bool HasComponentAddress(EntityId id) const {
		return id < component_offsets_.size() && component_offsets_[id] != INVALID_OFFSET;
	}
	// Determine whether or not the pool is a null pool
	bool IsValid() {
		return pool_ != nullptr && destructor_ != nullptr && capacity_ != 0 && component_size_ != 0;
	}
private:
	// Initial pool memory allocation, only called once in constructor
	void AllocatePool(std::size_t starting_capacity) {
		assert(size_ == 0 && capacity_ == 0 && pool_ == nullptr && "Cannot call initial pool allocation on occupied pool");
		capacity_ = starting_capacity;
		void* memory = std::malloc(capacity_);
		assert(memory != nullptr && "Failed to allocate initial memory for pool");
		pool_ = static_cast<char*>(memory);
	}
	void ReallocatePoolIfNeeded(std::size_t new_capacity) {
		if (new_capacity >= capacity_) {
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			capacity_ = new_capacity * 2; // double pool capacity when cap is reached
			auto memory = std::realloc(pool_, capacity_);
			assert(memory != nullptr && "Failed to reallocate memory for pool");
			pool_ = static_cast<char*>(memory);
		}
	}
	// Returns the first valid component offset in the pool
	Offset AddToPool(EntityId id) {
		auto next_free_offset = INVALID_OFFSET;
		if (free_offsets_.size() > 0) {
			// use first free offset found in the pool
			auto it = free_offsets_.begin();
			next_free_offset = *it;
			free_offsets_.erase(it);
		} else {
			// use the end of the pool
			next_free_offset = size_;
			size_ += component_size_;
			ReallocatePoolIfNeeded(size_);
		}
		assert(next_free_offset != INVALID_OFFSET && "Could not find a valid pool offset for component");
		return next_free_offset;
	}
	void AddComponentOffset(EntityId id, Offset component_offset) {
		if (id >= component_offsets_.size()) {
			std::size_t new_size = id + 1;
			component_offsets_.resize(new_size, INVALID_OFFSET);
		}
		component_offsets_[id] = component_offset;
	}
	char* pool_{ nullptr };
	Destructor destructor_{ nullptr };
	std::size_t capacity_{ 0 };
	std::size_t size_{ 0 };
	std::size_t component_size_{ 0 };
	// Vector index is an EntityId, vector element is the corresponding entity's component's offset from the start of the pool memory block
	std::vector<Offset> component_offsets_;
	std::vector<Offset> free_offsets_;
};

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
	EntityVersion version = null_version;
	bool alive = false;
};

} // namespace internal

class Entity;
class Manager;

class BaseSystem { // system interface
public:
	virtual void Init(Manager* manager) = 0;
	virtual void Update() = 0;
	virtual Manager& GetManager() = 0;
	virtual bool DependsOn(ComponentId component_id) const = 0;
	virtual void EntityChanged() = 0;
	virtual ~BaseSystem() {}
};

class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_ (null entity)
	Manager() : entities_{ internal::EntityData{} }, id_{ ++manager_count_ } {}
	~Manager() {
		id_ = internal::null_manager_id;
		entity_count_ = 0;
		entities_.clear();
		// Free component pools and call destructors on everything
		pools_.clear();
	}
	// Managers should never be copied
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&& obj) noexcept : id_{ obj.id_ }, entity_count_{ obj.entity_count_ }, entities_{ std::move(obj.entities_) }, pools_{std::move(obj.pools_)} {}
	Manager& operator=(Manager&& obj) = delete;
	friend class Entity;
	Entity CreateEntity();
	void DestroyEntity(Entity entity);
	template <typename T>
	void AddSystem();
	template <typename T>
	void Update();
	template <typename T>
	void ForEachEntity(T&& function, bool refresh_manager_after_completion = true);
	// Populates a lambda's parameter list with a reference to the entity and to the components in its type list
	template <typename ...Ts, typename T>
	void ForEach(T&& function, bool refresh_manager_after_completion = true);
	std::vector<Entity> GetEntities();
	template <typename ...Ts>
	std::vector<Entity> GetEntitiesWith();
	template <typename T, typename ...TArgs>
	T& AddComponent(Entity entity, TArgs&&... args);
	template <typename T>
	void RemoveComponent(Entity entity);
	template <typename ...Ts>
	void RemoveComponents(Entity entity);
	template <typename T>
	T& GetComponent(Entity entity) const;
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(Entity entity) const;
	template <typename T>
	bool HasComponent(Entity entity) const;
	template <typename ...Ts>
	bool HasComponents(Entity entity) const;
	bool IsAlive(Entity entity) const;
	void Refresh() {
		for (auto dead_id : dead_entities_) {
			// Destroy all of the entity's components
			for (auto& pool : pools_) {
				pool.RemoveComponentAddress(dead_id);
			}
			// Increment entity version, this will invalidate all entity handles with the previous version
			assert(dead_id < entities_.size() && "Could not find dead entity id when refreshing manager");
			++entities_[dead_id].version;
			entities_[dead_id].alive = false;
			free_entity_ids.emplace_back(dead_id);
		}
		dead_entities_.clear();
	}
private:
	void ComponentChanged(EntityId id, ComponentId component_id);
	void DestroyEntity(EntityId id, EntityVersion version) {
		if (IsAlive(id, version)) {
			// Add id to deletion list
			dead_entities_.emplace_back(id);
		}
	}
	template <typename ...Ts, typename T, std::size_t... component_id>
	void ForEachHelper(T&& function, EntityId id, std::vector<ComponentId>& component_ids, std::index_sequence<component_id...>);
	template <typename ...Ts, typename T>
	void ForEachInvoke(T&& function, EntityId id, std::vector<ComponentId>& component_ids) {
		ForEachHelper<Ts...>(std::forward<T>(function), id, component_ids, std::make_index_sequence<sizeof...(Ts)>{});
	}
	template <typename T>
	internal::ComponentPool& AddOrGetPool(ComponentId component_id) {
		if (component_id >= pools_.size()) {
			pools_.resize(component_id + 1);
		}
		auto& pool = pools_[component_id];
		if (!pool.IsValid()) {
			pool = std::move(internal::ComponentPool{ sizeof(T), &internal::DestroyComponent<T> });
		}
		assert(pool.IsValid() && "Could not find or create a valid pool for the component");
		return pool;
	}
	bool IsValid(SystemId id) const {
		return id < systems_.size() && systems_[id] != nullptr;
	}
	bool IsValid(EntityId id) const {
		return id != null && id < entities_.size() && entities_[id].version != null_version;
	}
	bool IsAlive(EntityId id, EntityVersion version) const {
		return IsValid(id) && entities_[id].alive && entities_[id].version == version;
	}
	template <typename T>
	T& GetComponent(EntityId id, ComponentId component_id) const {
		assert(IsValid(id) && "Cannot get component from invalid entity");
		const auto& pool = pools_[component_id];
		void* component_location = pool.GetComponentAddress(id);
		assert(component_location != nullptr && "Cannot get component which does not exist");
		return *static_cast<T*>(component_location);
	}
	template <typename T>
	T& GetComponent(EntityId id) const {
		return GetComponent<T>(id, internal::GetComponentId<T>());
	}
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(EntityId id) const {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id)...);
	}
	template <typename ...Ts>
	void RemoveComponents(EntityId id) {
		(RemoveComponent<Ts>(id), ...);
	}
	bool HasComponent(EntityId id, ComponentId component_id) const {
		assert(IsValid(id) && "Cannot check if invalid entity has component");
		return component_id < pools_.size() && pools_[component_id].HasComponentAddress(id);
	}
	template <typename T>
	bool HasComponent(EntityId id) const {
		return HasComponent(id, internal::GetComponentId<T>());
	}
	template <typename ...Ts>
	bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}
	bool HasComponents(EntityId id, std::vector<ComponentId>& component_ids) const {
		assert(IsValid(id) && "Cannot check if invalid entity has components");
		for (auto component_id : component_ids) {
			if (!HasComponent(id, component_id)) return false;
		}
		return true;
	}
	template <typename T, typename ...TArgs>
	T& AddComponent(EntityId id, TArgs&&... args) {
		assert(IsValid(id) && "Cannot add component to invalid entity");
		auto component_id = internal::GetComponentId<T>();
		void* component_location = nullptr;
		auto& pool = AddOrGetPool<T>(component_id);
		component_location = pool.GetComponentAddress(id);
		bool new_component = false;
		if (!component_location) { // if component address doesn't exist, add it
			component_location = pool.AddComponentAddress(id);
			new_component = true;
		}
		assert(component_location != nullptr && "Could not add component address");
		new(component_location) T(std::forward<TArgs>(args)...);
		if (new_component) {
			ComponentChanged(id, component_id);
		}
		return *static_cast<T*>(component_location);
	}
	template <typename T>
	void RemoveComponent(EntityId id) {
		assert(IsValid(id) && "Cannot remove component from invalid entity");
		auto component_id = internal::GetComponentId<T>();
		if (HasComponent(id, component_id)) {
			auto& pool = pools_[component_id];
			pool.RemoveComponentAddress(id);
			ComponentChanged(id, component_id);
		}
	}
	void GrowEntitiesIfNeeded(EntityId id) {
		if (id >= entities_.size()) {
			// Double entity capacity when limit is reached
			entities_.resize(entities_.capacity() * 2);
		}
	}
	EntityId entity_count_{ 0 };
	ManagerId id_{ internal::null_manager_id };
	std::vector<internal::EntityData> entities_;
	std::vector<internal::ComponentPool> pools_;
	std::vector<EntityId> dead_entities_;
	std::deque<EntityId> free_entity_ids;
	std::vector<std::unique_ptr<BaseSystem>> systems_;
	static ManagerId manager_count_;
};

ManagerId Manager::manager_count_ = internal::null_manager_id;

template <typename ...Cs>
class System : public BaseSystem {
public:
	System() {
		(AddComponent<Cs>(), ...);
	}
	// Call initial cache update
	virtual void Init(Manager* manager) override final;
	virtual ~System() override {}
	virtual Manager& GetManager() override final {
		assert(manager_ != nullptr && "Cannot get manager as system has not been properly initialized");
		return *manager_;
	}
	virtual void EntityChanged() override final {
		ResetCache();
		/*
		// Checking whether entity exists isn't needed as this function is only entered upon addition / removal of a NEW component, therefore entity could not have existed before / not existed before
		auto entity_exists = std::find_if(entities.begin(), entities.end(), [id](const Components& t) {
			return std::get<Entity>(t).GetId() == id;
		});
		if (entity_exists != / == entities.end()) { // entity found / not found in cache
			// Do stuff to cache
		}
		*/
	}
	virtual bool DependsOn(ComponentId component_id) const override final {
		return component_id < component_bitset_.size() && component_bitset_[component_id];
	}
protected:
	using Components = std::tuple<Entity, Cs&...>;
	std::vector<Components> entities;
private:
	void ResetCache();
	template <typename C>
	void AddComponent() {
		ComponentId component_id = internal::GetComponentId<C>();
		if (component_id >= component_bitset_.size()) {
			component_bitset_.resize(component_id + 1, false);
		}
		component_bitset_[component_id] = true;
	}
	std::vector<bool> component_bitset_;
	Manager* manager_;
};

class Entity {
public:
	Entity() = default;
	Entity(EntityId id, EntityVersion version, Manager* manager) : id_{ id }, version_{ version }, manager_{ manager } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	friend class Manager;
	EntityId GetId() const {
		return id_;
	}
	EntityVersion GetVersion() const {
		return version_;
	}
	Manager* GetManager() const {
		return manager_;
	}
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(id_, version_);
	}
	bool IsValid() const {
		return manager_ != nullptr && manager_->IsValid(id_);
	}
	template <typename T, typename ...TArgs>
	T& AddComponent(TArgs&&... args) {
		assert(IsValid() && "Cannot add component to null entity");
		assert(IsAlive() && "Cannot add component to dead entity");
		return manager_->AddComponent<T>(id_, std::forward<TArgs>(args)...);
	}
	template <typename T>
	void RemoveComponent() {
		assert(IsValid() && "Cannot remove component from null entity");
		assert(IsAlive() && "Cannot remove component from dead entity");
		return manager_->RemoveComponent<T>(id_);
	}
	template <typename ...Ts>
	void RemoveComponents() {
		assert(IsValid() && "Cannot remove components from null entity");
		assert(IsAlive() && "Cannot remove components from dead entity");
		return manager_->RemoveComponents<Ts...>(id_);
	}
	template <typename T>
	bool HasComponent() {
		assert(IsValid() && "Cannot check if null entity has a component");
		assert(IsAlive() && "Cannot check if dead entity has a component");
		return manager_->HasComponent<T>(id_);
	}
	template <typename ...Ts>
	bool HasComponents() {
		assert(IsValid() && "Cannot check if null entity has components");
		assert(IsAlive() && "Cannot check if dead entity has components");
		return manager_->HasComponents<Ts...>(id_);
	}
	template <typename T>
	T& GetComponent() {
		assert(IsValid() && "Cannot get component from null entity");
		assert(IsAlive() && "Cannot get component from dead entity");
		return manager_->GetComponent<T>(id_);
	}
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents() {
		assert(IsValid() && "Cannot get components from null entity");
		assert(IsAlive() && "Cannot get components from dead entity");
		return manager_->GetComponents<Ts...>(id_);
	}
	void Destroy(bool refresh_after = true) {
		if (IsAlive()) {
			manager_->DestroyEntity(id_, version_);
			if (refresh_after) {
				manager_->Refresh();
			}
		}
	}
	friend inline bool operator==(const Entity& lhs, const Entity& rhs) {
		return lhs.manager_ == rhs.manager_ && lhs.id_ == rhs.id_ && lhs.version_ == rhs.version_;
	}
	friend inline bool operator!=(const Entity& lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
private:
	EntityId id_ = null;
	EntityVersion version_ = null_version;
	Manager* manager_ = nullptr;
};

Entity Manager::CreateEntity() {
	EntityId id = null;
	if (free_entity_ids.size() > 0) { // pick id from free list before trying to increment counter
		id = free_entity_ids.front();
		free_entity_ids.pop_front();
	} else {
		id = ++entity_count_;
	}
	assert(id != null && "Could not create entity due to lack of free entity ids");
	GrowEntitiesIfNeeded(id);
	entities_[id].alive = true;
	return Entity{ id, ++entities_[id].version, this };
}
void Manager::DestroyEntity(Entity entity) {
	DestroyEntity(entity.id_, entity.version_);
}
std::vector<Entity> Manager::GetEntities() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	for (EntityId id = first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (entity_data.alive) {
			entities.emplace_back(id, entity_data.version, this);
		}
	}
	entities.shrink_to_fit();
	return entities; 
}
template <typename ...Ts>
std::vector<Entity> Manager::GetEntitiesWith() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	std::vector<ComponentId> component_ids = { internal::GetComponentId<Ts>()... };
	for (EntityId id = first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (HasComponents(id, component_ids) && entity_data.alive) {
			entities.emplace_back(id, entity_data.version, this);
		}
	}
	entities.shrink_to_fit();
	return entities;
}
template <typename ...Ts, typename T, std::size_t... component_id>
void Manager::ForEachHelper(T&& function, EntityId id, std::vector<ComponentId>& component_ids, std::index_sequence<component_id...>) {
	function(Entity{ id, entities_[id].version, this }, GetComponent<Ts>(id, component_ids[component_id])...);
}
template <typename ...Ts, typename T>
void Manager::ForEach(T&& function, bool refresh_manager_after_completion) {
	// TODO: write some tests for lambda function parameters
	std::vector<ComponentId> component_ids = { internal::GetComponentId<Ts>()... };
	for (EntityId id = first_valid_entity_id; id <= entity_count_; ++id) {
		if (HasComponents(id, component_ids) && entities_[id].alive) {
			ForEachInvoke<Ts...>(std::forward<T>(function), id, component_ids);
		}
	}
	if (refresh_manager_after_completion) {
		Refresh();
	}
}
template <typename T>
void Manager::ForEachEntity(T&& function, bool refresh_manager_after_completion) {
	// TODO: write some tests for lambda function parameters
	for (EntityId id = first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (entity_data.alive) {
			function(Entity{ id, entity_data.version, this });
		}
	}
	if (refresh_manager_after_completion) {
		Refresh();
	}
}
template <typename T, typename ...TArgs>
T& Manager::AddComponent(Entity entity, TArgs&&... args) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot add component to dead entity");
	return AddComponent<T>(entity.id_, std::forward<TArgs>(args)...);
}
template <typename T>
void Manager::RemoveComponent(Entity entity) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot remove component from dead entity");
	RemoveComponent<T>(entity.id_);
}
template <typename ...Ts>
void Manager::RemoveComponents(Entity entity) {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot remove components from dead entity");
	(RemoveComponent<Ts>(entity.id_), ...);
}
template <typename T>
T& Manager::GetComponent(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot get component from dead entity");
	return GetComponent<T>(entity.id_, internal::GetComponentId<T>());
}
template <typename ...Ts>
std::tuple<Ts&...> Manager::GetComponents(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot get components from dead entity");
	return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(entity.id_, internal::GetComponentId<Ts>())...);
}
template <typename T>
bool Manager::HasComponent(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot check if dead entity has a component");
	return HasComponent(entity.id_, internal::GetComponentId<T>());
}
template <typename ...Ts>
bool Manager::HasComponents(Entity entity) const {
	assert(IsAlive(entity.id_, entity.version_) && "Cannot check if dead entity has components");
	return (HasComponent<Ts>(entity.id_) && ...);
}
bool Manager::IsAlive(Entity entity) const {
	return IsAlive(entity.id_, entity.version_);
}
void Manager::ComponentChanged(EntityId id, ComponentId component_id) {
	for (auto& system : systems_) {
		if (system->DependsOn(component_id)) {
			assert(IsValid(id) && "Cannot trigger component change event for invalid entity");
			system->EntityChanged();
		}
	}
}
template <typename T>
void Manager::AddSystem() {
	static_assert(std::is_base_of_v<BaseSystem, T>, "Cannot add system which does not inherit from ecs::System");
	SystemId system_id = internal::GetSystemId<T>();
	if (system_id >= systems_.size()) {
		systems_.resize(system_id + 1);
	}
	systems_[system_id] = std::make_unique<T>();
	systems_[system_id]->Init(this);
}
template <typename T>
void Manager::Update() {
	static_assert(std::is_base_of_v<BaseSystem, T>, "Cannot update system which does not inherit from ecs::System");
	SystemId system_id = internal::GetSystemId<T>();
	assert(IsValid(system_id) && "Cannot update system which does not exist in manager");
	systems_[system_id]->Update();
}
template <typename ...Cs>
void System<Cs...>::Init(Manager* manager) {
	manager_ = manager;
	assert(manager_ != nullptr && "Could not initialize system with invalid manager");
	ResetCache();
}
template <typename ...Cs>
void System<Cs...>::ResetCache() {
	entities.clear();
	// TODO: Instead of calling GetEntitiesWith, call a function which directly gives a vector of tuples and instead of checking HasComponents it uses a GetComponentPointer with an if statement check to see if component exists
	auto matching_entities = manager_->GetEntitiesWith<Cs...>();
	entities.reserve(matching_entities.size());
	for (auto& entity : matching_entities) {
		entities.emplace_back(entity, entity.GetComponent<Cs>()...);
	}
}

} // namespace ecs