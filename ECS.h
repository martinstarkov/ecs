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

template <typename ...Ts>
using ComponentVector = std::vector<std::tuple<Entity, Ts&...>>;

class BaseCache {
public:
	virtual void UpdateCache() = 0;
	virtual bool IsValid() = 0;
	virtual void Invalidate() = 0;
};

template<typename T, typename... Ts>
constexpr bool UsesComponent() {
	return std::disjunction_v<std::is_same<T, Ts>...>;
}

template <typename ...Ts>
class Cache : public BaseCache {
public:
	Cache(Manager& manager) : manager_{ manager } {
		UpdateCache();
	}
	ComponentVector<Ts...> GetEntities() const;
	void UpdateCache() override final;
	bool IsValid() override final { return valid_; }
	void Invalidate() override final { valid_ = false; }
private:
	bool valid_ = true;
	Manager& manager_;
	ComponentVector<Ts...> entity_components_;
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
			assert(i < entities_.size());
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
		UpdateDestroyedEntities();
		RevalidateCaches();
	}
	void InvalidateCaches() {
		// TODO: Make a check which only invalidates relevant caches using template comparison
		for (auto& cache : caches_) {
			cache->Invalidate();
		}
	}
	template <typename ...Ts>
	Cache<Ts...>& AddCache() {
		return *static_cast<Cache<Ts...>*>(caches_.emplace_back(std::make_unique<Cache<Ts...>>(*this)).get());
	}
	template <typename ...Ts>
	ComponentVector<Ts...> GetEntities();
private:
	inline bool IsAlive(EntityId id) const {
		assert(HasEntity(id) && "Cannot check if nonexistent entity is alive");
		return entities_[id].alive;
	}
	inline bool HasEntity(EntityId id) const {
		return id != null && id < entities_.size();
	}
	void DestroyEntity(EntityId id) {
		destroyed_entities_.push_back(id);
		InvalidateCaches();
	}
	void RevalidateCaches() {
		for (auto& cache : caches_) {
			if (!cache->IsValid()) {
				cache->UpdateCache();
			}
		}
	}
	void UpdateDestroyedEntities() {
		for (auto id : destroyed_entities_) {
			assert(HasEntity(id) && "Cannot destroy nonexistent entity");
			assert(IsAlive(id) && "Cannot destroy dead entity");
			auto& pool = entities_[id];
			// Reset pool memory
			std::memset(data_ + pool.offset, 0, pool.capacity);
			// Add deleted pool offset to free memory list
			free_memory_.emplace(pool.capacity, pool.offset);
			pool.alive = false;
			pool.components.clear();
			pool.size = 0;
		}
		destroyed_entities_.clear();
	}
	inline bool HasDestructor(ComponentId component_id) const {
		return component_id < destructors_.size() && destructors_[component_id] != nullptr;
	}
	inline bool HasComponent(EntityId id, ComponentId component_id) const {
		assert(HasEntity(id) && "Cannot check if nonexistent entity has component");
		assert(IsAlive(id) && "Cannot check if dead entity has component");
		auto& components = entities_[id].components;
		return component_id < components.size() && components[component_id] != NULL_COMPONENT;
	}
	template <typename T>
	inline bool HasComponent(EntityId id) const {
		return HasComponent(id, GetComponentTypeId<T>());
	}
	template <typename T>
	void RemoveComponent(EntityId id) {
		assert(HasEntity(id) && "Cannot remove component from nonexistent entity");
		assert(IsAlive(id) && "Cannot remove component from dead entity");
		auto& pool = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		assert(HasComponent(id, component_id) && "Cannot remove component when it doesn't exist");
		auto& component_offset = pool.components[component_id];
		auto pool_location = data_ + pool.offset;
		auto component_location = pool_location + component_offset;
		assert(HasDestructor(component_id) && "Cannot call nonexistent destructor");
		auto destructor_function = destructors_[component_id];
		destructor_function(component_location);
		// TODO: Possibly shrink destructors_ if this is the final component of this type?
		// destructor_function = nullptr;

		// Clear component offset
		auto shifted_bytes = pool.capacity - (component_offset + sizeof(T));
		assert(shifted_bytes > 0 && "Cannot shift component memory block forward");
		std::memset(component_location, 0, sizeof(T));
		// Copy rest of bytes backward
		std::memcpy(component_location, component_location + sizeof(T), shifted_bytes);
		// Shift remaining offset by shifted amount;
		for (auto& component : pool.components) {
			if (component != NULL_COMPONENT && component > component_offset) {
				component -= sizeof(T);
				assert(component > 0 && "Components cannot have negative offsets");
			}
		}
		pool.size -= sizeof(T);
		assert(pool.size > 0 && "Cannot shrink pool below 0");
		component_offset = NULL_COMPONENT;
		InvalidateCaches();
	}
	std::size_t ComponentCount(EntityId id) const {
		assert(HasEntity(id) && "Cannot check component count of nonexistent entity");
		assert(IsAlive(id) && "Cannot check component count of dead entity");
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
		assert(HasEntity(id) && "Cannot replace component of nonexistent entity");
		assert(IsAlive(id) && "Cannot replace component of dead entity");
		auto& pool = entities_[id];
		assert(HasComponent(id, component_id) && "Cannot replace component when it doesn't exist");
		auto location = data_ + pool.offset + pool.components[component_id];
		// call destructor of previous component;
		assert(HasDestructor(component_id) && "Cannot call nonexistent destructor");
		auto destructor_function = destructors_[component_id];
		destructor_function(location);
		new(static_cast<void*>(location)) T(std::forward<TArgs>(args)...);
		return *static_cast<T*>(static_cast<void*>(location));
	}
	template <typename T, typename ...TArgs>
	T& AddComponent(EntityId id, TArgs&&... args) {
		// TODO: Add static assertion to check that args is valid
		assert(HasEntity(id) && "Cannot add component to nonexistent entity");
		assert(IsAlive(id) && "Cannot add component to dead entity");
		auto& pool = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		if (HasComponent(id, component_id)) {
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
		assert(component_id < destructors_.size() && "Component id out of range for destructors");
		assert(component_id < pool.components.size() && "Component id out of range for pool component offsets");
		destructors_[component_id] = &DestroyComponent<T>;
		pool.components[component_id] = pool.size;
		pool.size = new_pool_size;
		InvalidateCaches();
		return *static_cast<T*>(static_cast<void*>(data_ + offset));
	}
	template <typename T>
	T* GetComponentPointer(EntityId id) const {
		assert(HasEntity(id) && "Cannot get component pointer of nonexistent entity");
		assert(IsAlive(id) && "Cannot get component pointer of dead entity");
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
		assert(component != nullptr && "Cannot use GetComponent without HasComponent check for nonexistent components");
		return *component;
	}
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(EntityId id) const {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id)...);
	}
	inline void ResizeEntities(std::size_t new_capacity) {
		if (new_capacity > entities_.capacity()) {
			entities_.resize(new_capacity, { 0, 0, false });
		}
	}
	inline void ResizeComponents(EntityId id, std::size_t new_capacity) {
		assert(HasEntity(id) && "Cannot resize components of nonexistent entity");
		assert(IsAlive(id) && "Cannot resize components of dead entity");
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
		assert(id < entities_.size() && "Cannot add pool to entities_ vector");
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
	Byte capacity_{ 0 };
	Byte size_{ 0 };
	char* data_{ nullptr };
	EntityId entity_count_{ 0 };
	std::vector<EntityPool> entities_;
	std::vector<EntityId> destroyed_entities_;
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
		return manager_ != nullptr && id_ != null;
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
	EntityId id_;
	Manager* manager_;
};

Entity Manager::CreateEntity(Byte byte_capacity) {
	EntityId id{ ++entity_count_ };
	AddPool(id, byte_capacity);
	return Entity{ id, this };
}

template <typename ...Ts>
ComponentVector<Ts...> Manager::GetEntities() {
	ComponentVector<Ts...> vector;
	for (EntityId i = first_valid_entity; i <= entity_count_; ++i) {
		assert(i < entities_.size());
		if (entities_[i].alive && HasComponents<Ts...>(i)) {
			vector.emplace_back(Entity{ i, this }, GetComponent<Ts>(i)...);
		}
	}
	// NRVO? C:
	return vector;
}

template <typename ...Ts>
ComponentVector<Ts...> Cache<Ts...>::GetEntities() const {
	assert(valid_ == true && "Cache has been invalidated by an entity change");
	return entity_components_;
}

template <typename ...Ts>
void Cache<Ts...>::UpdateCache() {
	entity_components_ = manager_.GetEntities<Ts...>();
	valid_ = true;
}

} // namespace ecs