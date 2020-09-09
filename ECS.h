#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <type_traits>
#include <initializer_list>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cmath>

#include <atomic>

#include <string>

#include <cassert>

// TODO: Add license at top of file when you get there

// Do not use internal functions outside API
namespace internal {

} // namespace internal

// Entity Component System
namespace ecs {

using EntityId = std::uint32_t;
using ComponentId = std::uint32_t;
using AtomicComponentId = std::atomic_uint32_t;
using Byte = std::int64_t;

constexpr EntityId null = 0;
constexpr EntityId first_valid_entity = null + 1;
constexpr Byte NULL_COMPONENT = -1;

constexpr Byte DEFAULT_POOL_CAPACITY = 256;

extern AtomicComponentId component_counter;

template <typename T>
ComponentId GetComponentTypeId() {
	static ComponentId id = ++component_counter;
	return id;
}

AtomicComponentId component_counter{ 0 };

template<typename T>
void DestroyComponent(char* ptr) {
	static_cast<T*>(static_cast<void*>(ptr))->~T();
}

typedef void (*destructor)(char*);

class Manager;
class Entity;

class BaseCache {
public:
	virtual void UpdateCache() = 0;
};

template <typename ...Ts>
class Cache : public BaseCache {
public:
	using VectorOfTuples = std::vector<std::tuple<Entity&, Ts&...>>;
	Cache(Manager& manager) : manager_{ manager } {
		UpdateCache();
	}
	VectorOfTuples GetEntities() const {
		return entity_components_;
	}
	void UpdateCache() override final;
private:
	Manager& manager_;
	VectorOfTuples entity_components_;
};

struct EntityPool {
	EntityPool() = delete;
	// offset 0 indicates entity is destroyed / not in use (first byte of memory array)
	EntityPool(Byte offset, Byte capacity = 0, bool alive = false) : offset{ offset }, capacity{ capacity }, alive{ alive }, size{ 0 } {}
	Byte offset;
	Byte capacity;
	Byte size;
	bool alive;
	std::vector<Byte> components;
};

// TODO: Add remove component, remove entity, make those call destructors properly and work with manager update
// TODO: Add systems

class Manager {
public:
	// Important: Initialize a null entity in entities_
	Manager() : Manager{ 0, 0 } {}
	// Initialize null_entity
	Manager(EntityId entities, std::size_t components) : entities_{ { 0, 0, true } }, id_{ manager_count_++ } {
		AllocateData(2);
		// TODO: Move this out of here?
		Resize(entities * DEFAULT_POOL_CAPACITY, entities, components);
	}
	// Free memory block and call destructors on everything
	~Manager() = default;
	// Implement copying / moving of manager later
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&&) = delete;
	Manager& operator=(Manager&&) = delete;
	friend class Entity;
	// Capacity of initial entity pool allocated (in bytes)
	Entity CreateEntity(Byte byte_capacity = DEFAULT_POOL_CAPACITY);
	friend inline bool operator==(const Manager& lhs, const Manager& rhs) {
		return lhs.id_ == rhs.id_;
	}
	template <typename ...Ts, typename T>
	void ForEach(T&& function) {
		// TODO: write some tests for lambda parameters
		for (EntityId i = first_valid_entity; i < entity_count_; ++i) {
			auto& entity = entities_[i];
			if (entity.alive && HasComponents<Ts...>(i)) {
				function(i, (*static_cast<Ts*>(static_cast<void*>(data_ + entity.offset + entity.components[GetComponentTypeId<Ts>()])))...);
			}
		}
	}
	void Resize(Byte bytes, EntityId entities, std::size_t components = 0) {
		// Allocate at least one byte per new entity
		ReallocateData(bytes);
		ResizeEntities(entities);
		if (components) {
			for (auto i = first_valid_entity; i < entities_.size(); ++i) {
				ResizeComponents(i, components);
			}
		}
	}
	void Update() {
		UpdateCaches();
	}
	template <typename ...Ts>
	Cache<Ts...>& AddCache() {
		return *static_cast<Cache<Ts...>*>(caches_.emplace_back(std::make_unique<Cache<Ts...>>(*this)).get());
	}
private:
	inline bool HasEntity(EntityId id) const {
		return id != null && id < entities_.size() && entities_[id].alive;
	}
	void UpdateCaches() {
		if (entity_changed_) {
			for (auto& cache : caches_) {
				cache->UpdateCache();
			}
			entity_changed_ = false;
		}
	}
	void DestroyEntity(EntityId id) {
		auto& pool = entities_[id];
		// Only destroy entity if it is alive
		if (pool.alive) {
			// Reset pool memory
			std::memset(data_ + pool.offset, 0, pool.capacity);
			// Add deleted pool offset to free memory list
			free_memory_.emplace(pool.capacity, pool.offset);
			pool.alive = false;
			pool.components.clear();
			pool.size = 0;
		}
	}
	inline bool HasComponent(EntityId id, ComponentId component_id) const {
		auto& components = entities_[id].components;
		return id < components.size() && components[component_id] != NULL_COMPONENT;
	}
	template <typename T>
	inline bool HasComponent(EntityId id) const {
		return HasComponent(GetComponentTypeId<T>());
	}
	template <typename T>
	void RemoveComponent(EntityId id) {
		assert(HasEntity(id));
		auto& pool = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		auto& component_offset = pool.components[component_id];
		auto pool_location = data_ + pool.offset;
		auto component_location = pool_location + component_offset;
		auto& destructor_function = destructors_[component_id];
		assert(destructor_function != nullptr && "Could not find component destructor");
		destructor_function(component_location);
		destructor_function = nullptr;
		// Clear component offset
		// TODO: Possibly shrink destructors_ if this is the final component of this type?
		auto shifted_bytes = pool.capacity - (component_offset + sizeof(T));
		assert(shifted_bytes > 0 && "Cannot shift memory forward");
		std::memset(component_location, 0, sizeof(T));
		// Copy rest of bytes backward
		std::memcpy(component_location, component_location + sizeof(T), shifted_bytes);
		// Shift remaining offset by shifted amount;
		for (auto& component : pool.components) {
			if (component != NULL_COMPONENT && component > component_offset) {
				component -= sizeof(T);
				assert(component > 0 && "Component offset cannot be negative");
			}
		}
		pool.size -= sizeof(T);
		assert(pool.size > 0 && "Cannot shrink pool below 0");
		component_offset = NULL_COMPONENT;
	}
	std::size_t ComponentCount(EntityId id) const {
		assert(HasEntity(id));
		std::size_t count = 0;
		auto& pool = entities_[id];
		for (auto& component : pool.components) {
			if (component != NULL_COMPONENT) {
				++count;
			}
		}
		return count;
	}
	template <typename ...Ts>
	inline bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}
	template <typename T, typename ...TArgs>
	inline T& ReplaceComponent(EntityId id, TArgs&&... args) {
		return ReplaceComponent<T>(id, GetComponentTypeId<T>(), std::forward<TArgs>(args)...);
	}
	template <typename T, typename ...TArgs>
	T& ReplaceComponent(EntityId id, ComponentId component_id, TArgs&&... args) {
		assert(HasEntity(id));
		auto& pool = entities_[id];
		auto location = data_ + pool.offset + pool.components[component_id];
		// call destructor of previous component;
		auto destructor_function = destructors_[component_id];
		assert(destructor_function != nullptr && "Could not find component destructor");
		destructor_function(location);
		T& component = *static_cast<T*>(static_cast<void*>(location));
		component = T(std::forward<TArgs>(args)...);
		return component;
	}
	template <typename T, typename ...TArgs>
	T& AddComponent(EntityId id, TArgs&&... args) {
		// TODO: Add static assertion to check that args is valid
		assert(HasEntity(id));
		auto& pool = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		if (component_id < pool.components.size() && pool.components[component_id] != NULL_COMPONENT) {
			return ReplaceComponent<T>(id, component_id, std::forward<TArgs>(args)...);
		}
		Byte new_pool_size = pool.size + sizeof(T);
		if (new_pool_size > pool.capacity) {
			Byte new_capacity = new_pool_size * 2;
			MovePool(pool, GetFreeOffset(new_capacity), new_capacity);
		}
		Byte offset = pool.offset + pool.size;
		new(static_cast<void*>(data_ + offset)) T(std::forward<TArgs>(args)...);
		if (component_id >= pool.components.size()) {
			pool.components.resize(static_cast<std::size_t>(component_id) + 1, NULL_COMPONENT);
		}
		if (component_id >= destructors_.size()) {
			destructors_.resize(static_cast<std::size_t>(component_id) + 1, nullptr);
		}
		destructors_[component_id] = &DestroyComponent<T>;
		pool.components[component_id] = pool.size;
		pool.size = new_pool_size;
		// TODO: This invalidates caches?
		entity_changed_ = true;
		return *static_cast<T*>(static_cast<void*>(data_ + offset));
	}
	template <typename T>
	T* GetComponentPointer(EntityId id) const {
		assert(HasEntity(id));
		auto& entity = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		if (HasComponent(id, component_id)) {
			return static_cast<T*>(static_cast<void*>(data_ + entity.offset + entity.components[component_id]));
		}
		return nullptr;
	}
	template <typename T>
	T& GetComponent(EntityId id) const {
		T* component = GetComponentPointer<T>(id);
		assert(component != nullptr && "Cannot dereference nonexistent component");
		return *component;
	}
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(EntityId id) const {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id)...);
	}
	template <typename ...Ts>
	std::vector<std::tuple<Entity&, Ts&...>> GetEntities();
	inline void ResizeEntities(std::size_t new_capacity) {
		if (new_capacity > entities_.capacity()) {
			entities_.resize(new_capacity, { 0, 0, false });
		}
	}
	inline void ResizeComponents(EntityId id, std::size_t new_capacity) {
		assert(HasEntity(id));
		auto& components = entities_[id].components;
		if (new_capacity > components.capacity()) {
			components.resize(new_capacity, NULL_COMPONENT);
		}
	}
	void MovePool(EntityPool& pool, const Byte to_offset, const Byte to_capacity) {
		assert(pool.size > 0 && "Cannot move from empty pool");
		assert(pool.alive == true && "Cannot move from unoccupied pool");
		std::memcpy(data_ + to_offset, data_ + pool.offset, pool.size);
		std::memset(data_ + pool.offset, 0, pool.size);
		// Add old pool offset to free memory list
		free_memory_.emplace(pool.capacity, pool.offset);
		pool.offset = to_offset;
		pool.capacity = to_capacity;
	}
	void AddPool(EntityId id, Byte capacity) {
		// Double entity capacity when limit is reached
		if (id >= entities_.capacity()) {
			ResizeEntities(entities_.capacity() * 2);
		}
		assert(id < entities_.size());
		auto& pool = entities_[id];
		pool.offset = GetFreeOffset(capacity);
		pool.capacity = capacity;
		pool.alive = true;
	}
	// Return next matching capacity memory block in data_
	// If nothing existing found, take from end of data_
	Byte GetFreeOffset(Byte capacity) {
		auto it = free_memory_.find(capacity);
		// Get offset from free memory list
		if (it != std::end(free_memory_)) {
			return it->second;
		}
		// Get offset from end of memory block
		Byte free_offset = size_;
		size_ += capacity;
		if (size_ >= capacity_) {
			ReallocateData(size_ * 2);
		}
		return free_offset;
	}
	// Called once in manager constructor
	void AllocateData(Byte new_capacity) {
		assert(!data_ && "Cannot call AllocateData more than once per manager");
		auto memory = static_cast<char*>(std::malloc(new_capacity));
		assert(memory && "Failed to allocate memory for manager");
		capacity_ = new_capacity;
		data_ = memory;
	}
	void ReallocateData(Byte new_capacity) {
		if (new_capacity > capacity_) {
			auto memory = static_cast<char*>(std::realloc(data_, new_capacity));
			assert(memory && "Failed to reallocate memory for manager");
			capacity_ = new_capacity;
			data_ = memory;
		}
	}

	// TODO: Map component ids to their corresponding destructor function (to clean everything on manager destruction)

	bool entity_changed_ = false;
	Byte capacity_{ 0 };
	Byte size_{ 0 };
	char* data_{ nullptr };
	EntityId entity_count_{ 0 };
	std::vector<EntityPool> entities_;
	std::unordered_map<Byte, Byte> free_memory_;
	std::vector<std::unique_ptr<BaseCache>> caches_;
	std::size_t id_;
	static std::vector<destructor> destructors_;
	static std::size_t manager_count_;
};

std::vector<destructor> Manager::destructors_;
std::size_t Manager::manager_count_ = 0;

class Entity {
public:
	Entity(EntityId id = null, Manager* manager = nullptr) : id_{ id }, manager_{ manager } {}
	~Entity() = default;
	Entity(const Entity& copy) = default;
	Entity& operator=(const Entity& copy) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	bool IsValid() const {
		return manager_ != nullptr && manager_->HasEntity(id_);
	}
	const EntityId GetId() const {
		assert(IsValid() && "Cannot call function on null entity");
		return id_;
	}
	template <typename T>
	inline T& GetComponent() const {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->GetComponent<T>(id_);
	}
	template <typename ...Ts>
	inline std::tuple<Ts&...> GetComponents() const {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->GetComponents<Ts...>(id_);
	}
	template <typename T>
	inline bool HasComponent() const {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->HasComponent<T>(id_);
	}
	template <typename ...Ts>
	inline bool HasComponents() const {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->HasComponents<Ts...>(id_);
	}
	template <typename T, typename ...TArgs>
	inline T& AddComponent(TArgs&&... args) {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->AddComponent<T>(id_, std::forward<TArgs>(args)...);
	}
	template <typename T, typename ...TArgs>
	inline T& ReplaceComponent(TArgs&&... args) {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->ReplaceComponent<T>(id_, std::forward<TArgs>(args)...);
	}
	template <typename T>
	inline void RemoveComponent() {
		assert(IsValid() && "Cannot call function on null entity");
		manager_->RemoveComponent<T>(id_);
	}
	inline std::size_t ComponentCount() const {
		assert(IsValid() && "Cannot call function on null entity");
		return manager_->ComponentCount(id_);
	}
	inline void Destroy() {
		assert(IsValid() && "Cannot call function on null entity");
		manager_->DestroyEntity(id_);
	}
	friend inline bool operator==(EntityId lhs, const Entity& rhs) {
		return lhs == rhs.id_;
	}
	friend inline bool operator==(const Entity& lhs, EntityId rhs) {
		return lhs.id_ == rhs;
	}
	friend inline bool operator==(const Entity& lhs, const Entity& rhs) {
		return lhs.id_ == rhs.id_ && lhs.manager_ == rhs.manager_;
	}
	friend inline bool operator!=(const Entity& lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
	friend inline bool operator!=(const Entity& lhs, EntityId rhs) {
		return !(lhs == rhs);
	}
	friend inline bool operator!=(EntityId lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
private:
	const EntityId id_;
	Manager* manager_;
};

Entity Manager::CreateEntity(Byte byte_capacity) {
	EntityId id{ ++entity_count_ };
	AddPool(id, byte_capacity);
	return Entity{ id, this };
}

template <typename ...Ts>
std::vector<std::tuple<Entity&, Ts&...>> Manager::GetEntities() {
	std::vector<std::tuple<Entity&, Ts&...>> vector;
	for (EntityId i = first_valid_entity; i < entity_count_; ++i) {
		if (entities_[i].alive && HasComponents<Ts...>(i)) {
			vector.emplace_back(i, GetComponent<Ts>(i)...);
		}
	}
	// NRVO? C:
	return vector;
}

template <typename ...Ts>
void Cache<Ts...>::UpdateCache() {
	entity_components_ = manager_.GetEntities<Ts...>();
}

} // namespace ecs