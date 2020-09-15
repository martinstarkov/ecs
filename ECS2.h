#pragma once
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <vector>
#include <cassert>
#include <memory>
#include <algorithm>
#include <utility>
#include <tuple>

#include <iostream>

namespace ecs {

using EntityId = std::uint64_t;
using EntityVersion = std::uint64_t;
using Byte = std::int64_t;
using ComponentId = std::uint64_t;
using AtomicComponentId = std::atomic_uint64_t;

constexpr EntityId null = 0;
constexpr EntityId first_valid_entity = null + 1;
constexpr EntityVersion null_version = 0;
constexpr Byte INVALID_COMPONENT_OFFSET = -1;

extern AtomicComponentId component_counter;
AtomicComponentId component_counter{ 0 };

template <typename T>
ComponentId GetComponentTypeId() {
	static ComponentId id = component_counter++;
	return id;
}

template<typename T>
void DestroyComponent(void* component_address) {
	static_cast<T*>(component_address)->~T();
}

typedef void (*destructor)(void*);


struct ComponentPool {
	ComponentPool() = delete;
	ComponentPool(Byte capacity, Byte offset) : capacity{ capacity }, offset{ offset } {}
	Byte capacity;
	Byte offset;
	Byte size = 0;
	void AddComponentOffset(EntityId id, Byte offset) {
		if (id >= component_offsets.size()) {
			component_offsets.resize(id + 1, INVALID_COMPONENT_OFFSET);
		}
		component_offsets[id] = offset;
	}
	bool HasComponentOffset(EntityId id) const {
		return id < component_offsets.size() && component_offsets[id] != INVALID_COMPONENT_OFFSET;
	}
	Byte GetOffset(EntityId id) const {
		if (id < component_offsets.size() && component_offsets[id] != INVALID_COMPONENT_OFFSET) {
			return component_offsets[id];
		}
		return INVALID_COMPONENT_OFFSET;
	}
	Byte GetComponentOffset(EntityId id) const {
		assert(HasComponentOffset(id) && "Cannot get component offset which does not exist");
		return component_offsets[id];
	}
	std::vector<Byte> component_offsets;
};

class Manager;

class ComponentPoolHandler {
public:
	ComponentPoolHandler() {
		CreateBlock(2);
	}
	// Free memory block and call destructors on everything
	~ComponentPoolHandler() = default;
	ComponentPoolHandler(const ComponentPoolHandler&) = delete;
	ComponentPoolHandler& operator=(const ComponentPoolHandler&) = delete;
	ComponentPoolHandler(ComponentPoolHandler&&) = delete;
	ComponentPoolHandler& operator=(ComponentPoolHandler&&) = delete;
	friend class Manager;
private:
	inline bool ComponentPoolExists(ComponentId component_id) const {
		return component_id < components_.size();
	}
	inline bool HasComponent(EntityId id, ComponentId component_id) const {
		return component_id < components_.size() && components_[component_id].HasComponentOffset(id);
	}
	inline void* GetLocation(EntityId id, ComponentId component_id) const {
		if (component_id < components_.size()) {
			auto& component_pool = components_[component_id];
			auto component_offset = component_pool.GetOffset(id);
			if (component_offset != INVALID_COMPONENT_OFFSET) {
				return static_cast<void*>(block_ + component_pool.offset + component_offset);
			}
		}
		return nullptr;
	}
	// Return location of newly added component
	inline void* AddToPool(EntityId id, ComponentId component_id, Byte component_size) {
		assert(component_id <= components_.size() && "Component id does not have a valid pool");
		ComponentPool* component_pool = nullptr;
		if (!ComponentPoolExists(component_id)) { // new component, create a pool
			component_pool = CreatePool(component_size);
		} else { // existing components, add to pool
			component_pool = &components_[component_id];
			assert(component_pool != nullptr && "Component pool cannot be accessed");
			Byte new_pool_size = component_pool->size + component_size;
			if (new_pool_size >= component_pool->capacity) { // pool full, move everything to bigger pool
				MovePool(component_pool, new_pool_size);
			}
		}
		assert(component_pool != nullptr && "Cannot add component to invalid pool");
		Byte component_offset = component_pool->size;
		component_pool->size += component_size;
		component_pool->AddComponentOffset(id, component_offset); // add component offset at entity id index in pool offsets
		return static_cast<void*>(block_ + component_pool->offset + component_offset);
	}
	// Modifies pool pointer to have larger capacity and a different offset
	void MovePool(ComponentPool* pool, Byte to_size) {
		assert(pool != nullptr && "Invalid component pool cannot be expanded");
		assert(pool->size > 0 && "Cannot move from empty pool");
		assert(pool->capacity > 0 && "Cannot move from pool with capacity 0");
		auto free_pool = FindAvailablePool(to_size * 2);
		assert(free_pool.first > pool->capacity && "Capacity of expanded pool must be larger");
		assert(pool->offset != free_pool.second && "Offset of new pool must be different");
		// Copy memory block according to the to/from pool offsets
		std::memcpy(block_ + free_pool.second, block_ + pool->offset, pool->size);
		// Set from_pool data to 0
		std::memset(block_ + pool->offset, 0, pool->size);
		// Find new pool of at least twice the capacity
		// Add old pool offset to free memory list
		free_memory_.emplace_back(pool->capacity, pool->offset);
		pool->capacity = free_pool.first;
		pool->offset = free_pool.second;
	}
	// return <Capacity, Offset> of next available pool that matches the required capacity
	std::pair<Byte, Byte> FindAvailablePool(Byte needed_capacity) {
		// Fetch offset from free pools
		for (auto it = std::begin(free_memory_); it != std::end(free_memory_);) {
			// Consider offset if the given capacity is at least needed capacity and no more than double the needed capacity
			if (it->first >= needed_capacity && it->first <= needed_capacity * 2) {
				auto free_pool = *it;
				it = free_memory_.erase(it);
				return free_pool;
			} else {
				++it;
			}
		}
		// If no matching capacity free pool is found, use the end of the memory block
		auto free_offset = size_;
		size_ += needed_capacity;
		GrowBlockIfNeeded(size_);
		return { needed_capacity, free_offset };
	}
	inline ComponentPool* CreatePool(Byte pool_capacity) {
		auto new_pool = FindAvailablePool(pool_capacity);
		return &components_.emplace_back(new_pool.first, new_pool.second);
	}
	// Called once in manager constructor
	void CreateBlock(Byte new_capacity) {
		assert(!block_ && "Cannot call allocate block more than once per manager");
		auto memory = std::malloc(new_capacity);
		assert(memory != nullptr && "Failed to allocate memory for manager");
		capacity_ = new_capacity;
		block_ = static_cast<char*>(memory);
	}
	void GrowBlockIfNeeded(Byte new_capacity) {
		if (new_capacity >= capacity_) {
			capacity_ = new_capacity * 2;
			assert(block_ != nullptr && "Block has not been allocated, check that the pool handler constructor is called");
			auto memory = std::realloc(block_, capacity_);
			assert(memory != nullptr && "Failed to reallocate memory for manager");
			block_ = static_cast<char*>(memory);
		}
	}
	Byte capacity_{ 0 };
	Byte size_{ 0 };
	char* block_{ nullptr };
	// Key: capacity of block, Value: offset of block
	std::vector<std::pair<Byte, Byte>> free_memory_;
	std::vector<ComponentPool> components_;
};

struct EntityData {
	EntityData() = default;
	EntityVersion version = null_version;
};

class Entity;

class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_
	Manager() : entities_{ EntityData{} }, id_{ manager_count_++ } {}
	// Free memory block and call destructors on everything
	~Manager() = default;
	// Implement copying / moving of manager later
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&&) = delete;
	Manager& operator=(Manager&&) = delete;
	friend class Entity;
	Entity CreateEntity();
	inline bool IsAlive(EntityId id, EntityVersion version) const {
		return EntityVersionMatches(id, version);
	}
	inline bool EntityVersionMatches(EntityId id, EntityVersion version) const {
		return EntityExists(id) && version != null_version && entities_[id].version == version;
	}
	inline bool EntityExists(EntityId id) const {
		return id < entities_.size() && id != null;
	}
	template <typename T>
	inline bool HasComponent(EntityId id) const {
		return HasComponent(id, GetComponentTypeId<T>());
	}
	inline bool HasComponent(EntityId id, ComponentId component_id) const {
		assert(EntityExists(id) && "Cannot check if nonexistent entity has component");
		return component_pools_.HasComponent(id, component_id);
	}
	inline bool HasComponents(EntityId id, std::vector<ComponentId>& ids) const {
		for (auto i : ids) {
			if (!component_pools_.HasComponent(id, i)) return false;
		}
		return true;
	}
	template <typename ...Ts>
	inline bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}

	template <typename T, typename ...TArgs>
	inline T* AddComponent(EntityId id, TArgs&&... args) {
		assert(EntityExists(id) && "Cannot add component to nonexistent entity");
		ComponentId component_id = GetComponentTypeId<T>();
		Byte component_size = sizeof(T);
		assert(!HasComponent(id, component_id));
		void* component_location = component_pools_.AddToPool(id, component_id, component_size);
		new(component_location) T(std::forward<TArgs>(args)...);
		return static_cast<T*>(component_location);
	}
	template <typename ...Ts, typename T>
	void ForEach(T&& function, bool refresh_after_completion = true);
	template <typename T>
	inline T* GetComponentPointer(EntityId id) const {
		assert(EntityExists(id) && "Cannot get component pointer to nonexistent entity");
		ComponentId component_id = GetComponentTypeId<T>();
		return static_cast<T*>(component_pools_.GetLocation(id, component_id));
	}
	template <typename T>
	inline T& GetComponent(EntityId id, ComponentId component_id) const {
		return *static_cast<T*>(component_pools_.GetLocation(id, component_id));
	}
	template <typename T>
	inline T& GetComponent(EntityId id) const {
		return *static_cast<T*>(component_pools_.GetLocation(id, GetComponentTypeId<T>()));
	}
	template <typename ...Ts>
	inline std::tuple<Ts&...> GetComponents(EntityId id) const {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id)...);
	}
private:
	// Double entity capacity when limit is reached
	inline void GrowEntitiesIfNeeded(EntityId id) {
		if (id >= entities_.size()) {
			entities_.resize(entities_.capacity() * 2);
		}
	}
	EntityId entity_count_{ 0 };
	// Unique manager id
	std::size_t id_;
	std::vector<EntityData> entities_;
	ComponentPoolHandler component_pools_;
	static std::size_t manager_count_;
	// Components are global among all managers, therefore their destructors must be in a static vector
	// Destructor index is ComponentId of the component which it destructs
	static std::vector<destructor> destructors_;
};
std::vector<destructor> Manager::destructors_;
std::size_t Manager::manager_count_ = 0;

class Entity {
public:
	Entity() = default;
	Entity(EntityId id, EntityVersion version, Manager* manager) : id_{id}, version_{version}, manager_{manager} {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity &) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	bool IsValid() {
		return manager_ != nullptr && manager_->EntityVersionMatches(id_, version_);
	}
	template <typename T, typename ...TArgs>
	inline T* AddComponent(TArgs&&... args) {
		assert(IsValid() && "Cannot add component to invalid entity");
		return manager_->AddComponent<T>(id_, std::forward<TArgs>(args)...);
	}
private:
	EntityId id_ = null;
	EntityVersion version_ = null_version;
	Manager* manager_ = nullptr;
};

Entity Manager::CreateEntity() {
	EntityId id{ ++entity_count_ };
	GrowEntitiesIfNeeded(id);
	return Entity{ id, ++entities_[id].version, this };
}

template <typename ...Ts, typename T, std::size_t... index>
static void invoke_helper(T&& function, Manager& ecs, EntityId id, std::vector<ComponentId>& vector, std::index_sequence<index...>) {
	function(ecs.GetComponent<Ts>(id, vector[index])...);
}

template <typename ...Ts, typename T>
static void invoke(T&& function, Manager& ecs, EntityId id, std::vector<ComponentId>& vector) {
	//constexpr auto Size = std::tuple_size<typename std::decay<Tup>::type>::value;
	invoke_helper<Ts...>(std::forward<T>(function), ecs, id, vector, std::make_index_sequence<sizeof...(Ts)>{});
}

template <typename ...Ts, typename T>
void Manager::ForEach(T&& function, bool refresh_after_completion) {
	// TODO: write some tests for lambda parameters
	//auto args = std::make_tuple(GetComponentTypeId<Ts>()...);
	std::vector<ComponentId> args = { GetComponentTypeId<Ts>()... };
	for (EntityId id = first_valid_entity; id <= entity_count_; ++id) {
		if (HasComponents(id, args)) {
			invoke<Ts...>(std::forward<T>(function), *this, id, args);
		}
	}
	/*if (refresh_after_completion) {
		Refresh();
	}*/
}

} // namespace ecs