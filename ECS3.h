#pragma once

#pragma once

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>
#include <atomic>

#include <iostream>

namespace ecs {

using EntityId = std::uint64_t;
using EntityVersion = std::uint32_t;
using Offset = std::int64_t;
using ComponentId = std::uint32_t;
using AtomicComponentId = std::atomic_uint32_t;

// Constants
constexpr EntityId null = 0;
constexpr EntityId first_valid_entity_id = null + 1;
constexpr EntityVersion null_version = 0;

namespace internal {

// Indicates a nonexistent component in a pool
constexpr Offset INVALID_OFFSET = -1;

// Global component ids for all managers
extern AtomicComponentId component_counter;
AtomicComponentId component_counter{ 0 };

// Unique id for each new component type
template <typename T>
static ComponentId GetComponentId() {
	static ComponentId id = component_counter++;
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
	ComponentPool() = delete;
	ComponentPool(std::size_t component_size, Destructor destructor) : component_size_{ component_size }, destructor_{ destructor } {
		AllocatePool(component_size);
		//std::cout << "ComponentPool Constructor" << std::endl;
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
				std::cout << e.what() << std::endl;
				abort();
			}
			// Invalidate pool state
			pool_ = nullptr;
			//std::cout << "ComponentPool Destructor" << std::endl;
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
	ComponentPool& operator=(ComponentPool&&) = delete;
	// Move operator used for vector of component pool resizing
	ComponentPool(ComponentPool&& obj) noexcept : pool_{ obj.pool_ }, destructor_{ obj.destructor_ }, capacity_{ obj.capacity_ }, size_{ obj.size_ }, component_size_{ obj.component_size_ }, component_offsets_{ std::move(obj.component_offsets_) }, free_offsets_{ std::move(free_offsets_) } {
		// This allows the obj destructor to be called without freeing the pool memory
		obj.pool_ = nullptr;
		//std::cout << "ComponentPool Move" << std::endl;
	}
	void* AddComponentAddress(EntityId id) {
		Offset component_offset = AddToPool(id);
		AddComponentOffset(id, component_offset);
		return static_cast<void*>(pool_ + component_offset);
	}
	void RemoveComponentAddress(EntityId id) {
		if (id < component_offsets_.size()) { // Id exists in component offsets
			auto& component_offset = component_offsets_[id];
			if (component_offset != INVALID_OFFSET) { // Component exists
				void* component_address = static_cast<void*>(pool_ + component_offset);
				 // destroy component at address
				destructor_(component_address);
				// set memory to 0 (freed in component pool destructor)
				std::memset(component_address, 0, component_size_);
				free_offsets_.emplace_back(component_offset);
				// invalidate component offset
				component_offset = INVALID_OFFSET;
			}
		}
	}
	void* GetComponentAddress(EntityId id) const {
		if (id < component_offsets_.size()) { // Id exists in component offsets
			auto component_offset = component_offsets_[id];
			if (component_offset != INVALID_OFFSET) { // Component exists
				return static_cast<void*>(pool_ + component_offset);
			}
		}
		return nullptr;
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
	EntityData(const EntityData& copy) noexcept : version{ copy.version } {}
	EntityData(EntityData&& obj) noexcept : version{ obj.version } {
		obj.version = null_version;
	}
	EntityVersion version = null_version;
};

} // namespace internal

class Entity;

class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_ (null entity)
	Manager() : entities_{ internal::EntityData{} }, id_{ manager_count_++ } {}
	~Manager() {
		entity_count_ = 0;
		entities_.clear();
		// Free component pools and call destructors on everything
		pools_.clear();
	}
	// Implement copying / moving of manager later
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&& obj) noexcept : entity_count_{ obj.entity_count_ }, entities_{ std::move(obj.entities_) }, pools_{ std::move(obj.pools_) } {}
	Manager& operator=(Manager&&) = delete;
	friend class Entity;
	Entity CreateEntity();
	bool IsAlive(EntityId id, EntityVersion version) const {
		return EntityVersionMatches(id, version);
	}
	bool EntityVersionMatches(EntityId id, EntityVersion version) const {
		return EntityExists(id) && version != null_version && entities_[id].version == version;
	}
	bool EntityExists(EntityId id) const {
		return id < entities_.size() && id != null;
	}
	template <typename T>
	bool HasComponent(EntityId id) const {
		return HasComponent(id, internal::GetComponentId<T>());
	}
	bool HasComponent(EntityId id, ComponentId component_id) const {
		assert(EntityExists(id) && "Cannot check if nonexistent entity has component");
		return component_id < pools_.size() && pools_[component_id].GetComponentAddress(id) != nullptr;
	}
	bool HasComponents(EntityId id, std::vector<ComponentId>& ids) const {
		for (auto i : ids) {
			if (i >= pools_.size() || pools_[i].GetComponentAddress(id) == nullptr) return false;
		}
		return true;
	}
	template <typename T>
	internal::ComponentPool& AddComponentPoolIfNeeded(ComponentId component_id) {
		assert(component_id <= pools_.size() && "Component addition failed due to pools_ resizing");
		if (component_id == pools_.size()) {
			return pools_.emplace_back(sizeof(T), &internal::DestroyComponent<T>);
		}
		return pools_[component_id];
	}
	template <typename ...Ts>
	bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}
	template <typename T, typename ...TArgs>
	T* AddComponent(EntityId id, TArgs&&... args) {
		assert(EntityExists(id) && "Cannot add component to nonexistent entity");
		auto component_id = internal::GetComponentId<T>();
		assert(!HasComponent(id, component_id));
		auto& pool = AddComponentPoolIfNeeded<T>(component_id);
		void* component_location = pool.AddComponentAddress(id);
		new(component_location) T(std::forward<TArgs>(args)...);
		return static_cast<T*>(component_location);
	}
	template <typename ...Ts, typename T>
	void ForEach(T&& function, bool refresh_after_completion = true); 
	template <typename T>
	T& GetComponent(EntityId id, ComponentId component_id) const {
		assert(HasComponent(id, component_id));
		const auto& pool = pools_[component_id];
		void* component_location = pool.GetComponentAddress(id);
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
private:
	// Double entity capacity when limit is reached
	void GrowEntitiesIfNeeded(EntityId id) {
		if (id >= entities_.size()) {
			entities_.resize(entities_.capacity() * 2);
		}
	}
	EntityId entity_count_{ 0 };
	// Unique manager id
	std::size_t id_;
	std::vector<internal::EntityData> entities_;
	std::vector<internal::ComponentPool> pools_;
	static std::size_t manager_count_;
};

std::size_t Manager::manager_count_ = 0;

class Entity {
public:
	Entity() = default;
	Entity(EntityId id, EntityVersion version, Manager* manager) : id_{ id }, version_{ version }, manager_{ manager } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	bool IsValid() {
		return manager_ != nullptr && manager_->EntityVersionMatches(id_, version_);
	}
	template <typename T, typename ...TArgs>
	T* AddComponent(TArgs&&... args) {
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
	//auto args = std::make_tuple(GetComponentId<Ts>()...);
	std::vector<ComponentId> args = { internal::GetComponentId<Ts>()... };
	for (EntityId id = first_valid_entity_id; id <= entity_count_; ++id) {
		if (HasComponents(id, args)) {
			invoke<Ts...>(std::forward<T>(function), *this, id, args);
		}
	}
	/*if (refresh_after_completion) {
		Refresh();
	}*/
}

} // namespace ecs