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
private:
	void UpdateCaches() {
		for (auto& cache : caches_) {
			cache->UpdateCache();
		}
	}
	void DestroyEntity(EntityId id) {
		entities_[id].alive = false;
	}
	template <typename T>
	bool HasComponent(EntityId id) const {
		auto& components = entities_[id].components;
		ComponentId component_id = GetComponentTypeId<T>();
		if (component_id < components.size()) {
			return components[component_id] != NULL_COMPONENT;
		}
		return false;
	}
	bool HasComponent(EntityId id, ComponentId component_id) const {
		auto& components = entities_[id].components;
		if (id < components.size()) {
			return components[component_id] != NULL_COMPONENT;
		}
		return false;
	}
	template <typename T>
	void RemoveComponent(EntityId id) {
		auto& components = entities_[id].components;
		// call destructor
		// clear offset and shift all offsets
	}
	template <typename ...Ts>
	bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}
	template <class T, typename ...TArgs>
	void AddComponent(EntityId id, TArgs&&... args) {
		assert(id < entities_.size());
		auto& pool = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		if (component_id < pool.components.size()) {
			if (pool.components[component_id] != NULL_COMPONENT) {
				// Replace component
				return;
			}
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
		pool.components[component_id] = pool.size;
		pool.size = new_pool_size;
		UpdateCaches();
	}
	template <typename T>
	T* GetComponentPointer(EntityId id) const {
		assert(id < entities_.size());
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
	std::vector<std::tuple<const EntityId, Ts&...>> GetMatchingEntities() {
		std::vector<std::tuple<const EntityId, Ts&...>> vector;
		for (EntityId i = first_valid_entity; i < entity_count_; ++i) {
			if (entities_[i].alive && HasComponents<Ts...>(i)) {
				vector.emplace_back(i, GetComponent<Ts>(i)...);
			}
		}
		// NRVO? C:
		return vector;
	}
	EntityId EntityCount() const {
		return entity_count_;
	}
	void Resize(Byte bytes, EntityId entities, std::size_t components = 0) {
		// Allocate at least one byte per new entity
		ReAllocateData(bytes);
		ResizeEntities(entities);
		if (components) {
			for (auto i = first_valid_entity; i < entities_.size(); ++i) {
				ResizeComponent(i, components);
			}
		}
	}
	template <typename ...Ts>
	Cache<Ts...>& AddCache() {
		return *static_cast<Cache<Ts...>*>(caches_.emplace_back(std::make_unique<Cache<Ts...>>(*this)).get());
	}
	void ResizeEntities(std::size_t new_capacity) {
		entities_.resize(new_capacity, { 0, 0, false });
	}
	void ResizeComponent(EntityId index, std::size_t new_capacity) {
		assert(index < entities_.size());
		entities_[index].components.resize(new_capacity, NULL_COMPONENT);
	}
	void MovePool(EntityPool& pool, Byte to_offset, Byte to_capacity) {
		assert(pool.size > 0 && "Cannot move from empty pool");
		assert(pool.alive == true && "Cannot move from unoccupied pool");
		std::memcpy(data_ + to_offset, data_ + pool.offset, pool.size);
		std::memset(data_ + pool.offset, 0, pool.size);
		free_memory_.emplace(pool.capacity, pool.offset);
		pool.offset = to_offset;
		pool.capacity = to_capacity;
	}
	void AddPool(EntityId index, Byte capacity) {
		// Double entities capacity when limit is reached
		if (index >= entities_.capacity()) {
			ResizeEntities(entities_.capacity() * 2);
		}
		auto& pool = entities_[index];
		pool.offset = GetFreeOffset(capacity);
		pool.capacity = capacity;
		pool.alive = true;
	}
	// Return next matching capacity memory block in data_
	// If nothing existing found, take from end of data_
	Byte GetFreeOffset(Byte capacity) {
		auto it = free_memory_.find(capacity);
		if (it != std::end(free_memory_)) {
			return it->second;
		}
		Byte free_offset = size_;
		size_ += capacity;
		if (size_ >= capacity_) {
			ReAllocateData(size_ * 2);
		}
		return free_offset;
	}
	// Only called once in constructor
	void AllocateData(Byte new_capacity) {
		assert(!data_);
		capacity_ = new_capacity;
		auto memory = static_cast<char*>(std::malloc(capacity_));
		assert(memory);
		data_ = memory;
	}
	void ReAllocateData(Byte new_capacity) {
		if (new_capacity > capacity_) {
			auto memory = static_cast<char*>(std::realloc(data_, new_capacity));
			assert(memory);
			data_ = memory;
			capacity_ = new_capacity;
		}
	}

	// TODO: Map component ids to their corresponding destructor function (to clean everything on manager destruction)

	bool entity_change = false;
	Byte capacity_{ 0 };
	Byte size_{ 0 };
	char* data_{ nullptr };
	EntityId entity_count_{ 0 };
	std::vector<EntityPool> entities_;
	std::unordered_map<Byte, Byte> free_memory_;
	std::vector<std::unique_ptr<BaseCache>> caches_;
	std::size_t id_;
	static std::size_t manager_count_;
};

std::size_t Manager::manager_count_ = 0;

class Entity {
public:
	Entity(EntityId id, Manager& manager) : id_{ id }, manager_{ manager } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	const EntityId GetId() const {
		return id_;
	}
	template <typename T>
	T& GetComponent() const {
		return manager_.GetComponent<T>(id_);
	}
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(EntityId id) const {
		return manager_.GetComponents<Ts...>(id);
	}
	template <typename T>
	bool HasComponent() const {
		return manager_.HasComponent<T>(id_);
	}
	template <typename T, typename ...TArgs>
	T& AddComponent(TArgs&&... args) {
		return manager_.AddComponent<T>(id_, std::forward<TArgs>(args)...);
	}
	template <typename T>
	void RemoveComponent() {
		manager_.RemoveComponent<T>(id_);
	}
	void Destroy() {
		manager_.DestroyEntity(id_);
	}
	friend inline bool operator==(const Entity& lhs, const Entity& rhs) {
		return lhs.id_ == rhs.id_ && lhs.manager_ == rhs.manager_;
	}
private:
	const EntityId id_;
	Manager& manager_;
};

Entity Manager::CreateEntity(Byte byte_capacity) {
	EntityId id{ ++entity_count_ };
	AddPool(id, byte_capacity);
	return Entity{ id, *this };
}

template <typename ...Ts>
void Cache<Ts...>::UpdateCache() {
	entity_components_ = manager_.GetMatchingEntities<Ts...>();
}

} // namespace ecs