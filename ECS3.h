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
using EntityVersion = std::int32_t;
using Offset = std::int64_t;
using ComponentId = std::uint32_t;
using AtomicComponentId = std::atomic_uint32_t;

constexpr EntityId null = 0;
constexpr EntityId first_valid_entity = null + 1;
constexpr EntityVersion null_version = 0;
constexpr Offset INVALID_OFFSET = -1;

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

typedef void (*Destructor)(void*);

class ComponentPool {
public:
	ComponentPool() = delete;
	ComponentPool(std::size_t component_size, Destructor destructor) : component_size_{ component_size }, destructor_{ destructor } {
		AllocatePool(2);
		//std::cout << "ComponentPool Constructor" << std::endl;
	}
	// Free memory block and call destructors on everything
	~ComponentPool() {
		if (pool_ != nullptr) { // Free memory block if not a move destructor call
			//std::cout << "ComponentPool Destructor" << std::endl;
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
			pool_ = nullptr;
			destructor_ = nullptr;
			capacity_ = 0;
			size_ = 0;
			component_size_ = 0;
			component_offsets_.clear();
			free_offsets_.clear();
		}
	}
	// Figure these out later
	ComponentPool(const ComponentPool&) = delete;
	ComponentPool& operator=(const ComponentPool&) = delete;
	ComponentPool& operator=(ComponentPool&& obj) = delete;
	ComponentPool(ComponentPool&& obj) noexcept : pool_{ obj.pool_ }, destructor_{ obj.destructor_ }, capacity_{ obj.capacity_ }, size_{ obj.size_ }, component_size_{ obj.component_size_ }, component_offsets_{ std::move(obj.component_offsets_) }, free_offsets_{ std::move(free_offsets_) } {
		//std::cout << "ComponentPool Move" << std::endl;
		obj.pool_ = nullptr;
		obj.destructor_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
		obj.component_size_ = 0;
		obj.component_offsets_.clear();
		obj.free_offsets_.clear();
	}
	void* AddComponentAddress(EntityId id) {
		Offset component = AddToPool(id);
		AddOffset(id, component);
		return static_cast<void*>(pool_ + component);
	}
	void RemoveComponentAddress(EntityId id) {
		// Id exists in component offsets
		if (id < component_offsets_.size()) {
			Offset& component = component_offsets_[id];
			if (component != INVALID_OFFSET) {
				// remove address and free memory
				void* component_address = static_cast<void*>(pool_ + component);
				destructor_(component_address);
				std::memset(component_address, 0, component_size_);
				free_offsets_.emplace_back(component);
				component = INVALID_OFFSET;
			}
		}
	}
	void* GetComponentAddress(EntityId id) const {
		// Id exists in component offsets
		if (id < component_offsets_.size()) {
			Offset component = component_offsets_[id];
			if (component != INVALID_OFFSET) {
				return static_cast<void*>(pool_ + component);
			}
		}
		return nullptr;
	}
private:
	void AllocatePool(std::size_t starting_capacity) {
		assert(size_ == 0 && capacity_ == 0 && pool_ == nullptr && "Cannot call initial pool allocation on occupied pool");
		capacity_ = starting_capacity;
		void* memory = std::malloc(capacity_);
		assert(memory != nullptr && "Failed to allocate initial memory for pool");
		pool_ = static_cast<char*>(memory);
	}
	void ReallocatePool(std::size_t new_capacity) {
		if (new_capacity >= capacity_) {
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			capacity_ = new_capacity * 2; // double pool capacity when cap is reached
			auto memory = std::realloc(pool_, capacity_);
			assert(memory != nullptr && "Failed to reallocate memory for pool");
			pool_ = static_cast<char*>(memory);
		}
	}
	//void RecompactPool() {
	//	// Sort free memory by descending order of offsets so memory shifting can happen from end to beginning
	//	std::sort(free_offsets_.begin(), free_offsets_.end(), [](const Offset lhs, const Offset rhs) {
	//		return lhs > rhs;
	//	});
	//	// calculate how much memory would be removed from empty
	//	std::size_t free_components = free_offsets_.size();
	//	static_assert(RECOMPACT_THRESHOLD > 0, "Cannot recompact pool with no free components");
	//	if (free_components >= RECOMPACT_THRESHOLD) {
	//		size_ -= free_components * component_size_;
	//		assert(size_ >= 0 && "Cannot shrink pool size below 0");
	//		RecompactPool();
	//	}
	//	for (auto i = 0; i < free_offsets_.size(); ++i) {
	//		Offset free_offset = free_offsets_[i];
	//		//std::memcpy();
	//	}
	//}
	Offset AddToPool(EntityId id) {
		Offset next;
		if (free_offsets_.size() > 0) {
			auto it = free_offsets_.begin();
			next = *it;
			free_offsets_.erase(it);
		} else {
			next = size_;
			size_ += component_size_;
			ReallocatePool(size_);
		}
		return next;
	}
	void AddOffset(EntityId id, Offset component) {
		if (id >= component_offsets_.size()) {
			std::size_t new_size = id + 1;
			component_offsets_.resize(new_size, INVALID_OFFSET);
		}
		component_offsets_[id] = component;
	}
	char* pool_{ nullptr };
	Destructor destructor_{ nullptr };
	std::size_t capacity_{ 0 };
	std::size_t size_{ 0 };
	std::size_t component_size_{ 0 };
	// Index is EntityId, element if the corresponding component's offset from the start of data_
	std::vector<Offset> component_offsets_;
	std::vector<Offset> free_offsets_;
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
		return component_id < pools_.size() && pools_[component_id].GetComponentAddress(id) != nullptr;
	}
	inline bool HasComponents(EntityId id, std::vector<ComponentId>& ids) const {
		for (auto i : ids) {
			if (i >= pools_.size() || pools_[i].GetComponentAddress(id) == nullptr) return false;
		}
		return true;
	}
	template <typename T>
	inline ComponentPool& AddComponentPoolIfNeeded(ComponentId component_id) {
		assert(component_id <= pools_.size() && "Component addition failed due to pools_ resizing");
		if (component_id == pools_.size()) {
			return pools_.emplace_back(sizeof(T), &DestroyComponent<T>);
		}
		return pools_[component_id];
	}
	template <typename ...Ts>
	inline bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}

	template <typename T, typename ...TArgs>
	inline T* AddComponent(EntityId id, TArgs&&... args) {
		assert(EntityExists(id) && "Cannot add component to nonexistent entity");
		ComponentId component_id = GetComponentTypeId<T>();
		assert(!HasComponent(id, component_id));
		ComponentPool& pool = AddComponentPoolIfNeeded<T>(component_id);
		void* component_location = pool.AddComponentAddress(id);
		new(component_location) T(std::forward<TArgs>(args)...);
		return static_cast<T*>(component_location);
	}
	template <typename ...Ts, typename T>
	void ForEach(T&& function, bool refresh_after_completion = true);
	/*template <typename T>
	inline T* GetComponentPointer(EntityId id) const {
		assert(EntityExists(id) && "Cannot get component pointer to nonexistent entity");
		ComponentId component_id = GetComponentTypeId<T>();
		return static_cast<T*>(component_pools_.GetLocation(id, component_id));
	}*/
	template <typename T>
	inline T& GetComponent(EntityId id, ComponentId component_id) const {
		assert(HasComponent(id, component_id));
		const auto& pool = pools_[component_id];
		void* component_location = pool.GetComponentAddress(id);
		return *static_cast<T*>(component_location);
	}
	template <typename T>
	inline T& GetComponent(EntityId id) const {
		return GetComponent<T>(id, GetComponentTypeId<T>());
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
	std::vector<ComponentPool> pools_;
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