#pragma once

#include <iostream> // std::cout
#include <vector> // std::vector
#include <memory> // std::unique_ptr
#include <algorithm> // std::sort
#include <cstdlib> // std::malloc
#include <atomic> // std::atomic_int
#include <cassert> // assert

//#define NDEBUG

//#if __cplusplus < 201703L
//#endif

#include <type_traits> // future template checks

// TODO: Add license at top of file when you get there

// Do not use internal functions outside API
namespace internal {

} // namespace internal

// Entity Component System
namespace ecs {

using EntityId = std::uint64_t;
using ComponentId = std::uint32_t;
using PoolIndex = std::int32_t;
using AtomicComponentId = std::atomic_uint32_t;
using Byte = std::int64_t;

// Invalid entity
constexpr EntityId null = 0;
// Used for internal for-loop indexing
constexpr EntityId first_valid_entity = null + 1;
// Represents a nonexistent component
constexpr Byte INVALID_COMPONENT_OFFSET = -1;
// Invalid pool, for checking if entity is destroyed
constexpr PoolIndex INVALID_POOL_INDEX = -1;
// This constant represents how many pool deletions should occur before compacting pool memory
constexpr std::uint16_t FREE_POOLS_BEFORE_COMPACTING = 5;

constexpr Byte DEFAULT_POOL_CAPACITY = 100; // 256 usually

extern AtomicComponentId component_counter;

template <typename T>
ComponentId GetComponentTypeId() {
	static ComponentId id = component_counter++;
	return id;
}

AtomicComponentId component_counter{ 0 };

template<typename T>
void DestroyComponent(void* component_address) {
	static_cast<T*>(component_address)->~T();
}

typedef void (*destructor)(void*);

class Manager;
class Entity;

template <typename ...Ts>
using ComponentVector = std::vector<std::tuple<Entity, Ts&...>>;

//class BaseCache {
//public:
//};

//template <typename ...Ts>
//class Cache : public BaseCache {
//public:
//	Cache(Manager& manager) : manager_{ manager } {
//		UpdateCache();
//	}
//	template <typename T>
//	void ForEach(T&& function);
//private:
//	void UpdateCache();
//	Manager& manager_;
//	ComponentVector<Ts...> entity_components_;
//};

// TODO: Fix cache refreshing
// TODO: Add vector of pool ids which will be entities, and a separate vector of pools which point to memory
// TODO: At every refresh call, update caches, shift memory to be compact, handle pool deletions
// TODO: Add systems

class EntityPoolHandler {
public:
	EntityPoolHandler() {
		// Allocate 2 bytes initially in order for capacity doubling to work (since 0 can't be doubled)
		CreateBlock(2);
	}
	// Free memory block and call destructors on everything
	~EntityPoolHandler() = default;
	// Implement copying / moving of manager later
	EntityPoolHandler(const EntityPoolHandler&) = delete;
	EntityPoolHandler& operator=(const EntityPoolHandler&) = delete;
	EntityPoolHandler(EntityPoolHandler&&) = delete;
	EntityPoolHandler& operator=(EntityPoolHandler&&) = delete;
	void* GetComponentAddress(PoolIndex index, ComponentId component_id) const {
		assert(HasPool(index) && "Cannot get component address from an entity pool which does not exist");
		auto& pool = pools_[index];
		if (pool.HasComponentOffset(component_id)) {
			return static_cast<void*>(block_ + pool.offset + pool.GetComponentOffset(component_id));
		}
		return nullptr;
	}
	Byte& GetComponentOffset(PoolIndex index, ComponentId component_id) {
		assert(HasPool(index) && "Cannot get component offset from an entity pool which does not exist");
		return pools_[index].GetComponentOffset(component_id);
	}
	Byte& GetComponentTracker(PoolIndex index, ComponentId component_id) {
		assert(HasPool(index) && "Cannot get component tracker from an entity pool which does not exist");
		return pools_[index].GetComponentTracker(component_id);
	}
	Byte GetComponentOffset(PoolIndex index, ComponentId component_id) const {
		assert(HasPool(index) && "Cannot get component offset from an entity pool which does not exist");
		return pools_[index].GetComponentOffset(component_id);
	}
	// Expand entity pool and return new valid component address
	void* CreateComponentAddress(PoolIndex index, ComponentId component_id, std::size_t component_size) {
		assert(HasPool(index) && "Cannot create component address for entity pool which does not exist");
		auto& pool = pools_[index];
		auto component_offset = ExpandPool(pool, component_size);
		pool.AddComponentOffset(component_id, component_offset);
		return static_cast<void*>(block_ + pool.offset + component_offset);
	}
	// Shift all component offsets above an offset by a certain amount of bytes
	void ShiftComponentOffsets(PoolIndex index, Byte removed_offset, Byte shift_amount) {
		assert(HasPool(index) && "Cannot shift component offsets for entity pool which does not exist");
		auto& pool = pools_[index];
		pool.ShiftComponentOffsets(removed_offset, shift_amount);
		// Set the leftover memory after shifting the components to 0
		std::memset(block_ + pool.offset + pool.size - shift_amount, 0, shift_amount);
		// Shrink pool size
		pool.size -= shift_amount;
		assert(pool.size >= 0 && "Cannot shrink entity pool below 0 bytes");
	}
	bool HasComponent(PoolIndex index, ComponentId component_id) const {
		assert(HasPool(index) && "Cannot check if nonexistent entity pool has component");
		return pools_[index].HasComponentOffset(component_id);
	}
	PoolIndex AddPool(Byte pool_capacity) {
		auto pool_index = static_cast<PoolIndex>(pools_.size());
		pools_.emplace_back(GetFreeOffset(pool_capacity), pool_capacity);
		return pool_index;
	}
	Byte GetPoolSize(PoolIndex index) {
		assert(HasPool(index) && "Cannot get size of nonexistent entity pool");
		return pools_[index].size;
	}
	Byte GetPoolCapacity(PoolIndex index) {
		assert(HasPool(index) && "Cannot get capacity of nonexistent entity pool");
		return pools_[index].capacity;
	}
	void FreePool(PoolIndex index, const std::vector<destructor>& destructors) {
		assert(HasPool(index) && "Cannot free nonexistent entity pool");
		auto pool_iterator = std::begin(pools_) + index;
		const auto& offsets = pool_iterator->GetComponentOffsets();
		// Cycle through all valid entity components and call their destructors
		for (auto component_id = 0; component_id < offsets.size(); ++component_id) {
			if (offsets[component_id] != INVALID_COMPONENT_OFFSET) {
				assert(component_id < destructors.size());
				auto destructor_function = destructors[component_id];
				assert(destructor_function != nullptr && "Cannot destroy entity as the manager could not find one of its component destructors");
				void* component_location = GetComponentAddress(index, component_id);
				assert(component_location != nullptr && "Cannot destruct nonexistent component");
				destructor_function(component_location);
			}
		}
		// Clear pool memory
		std::memset(block_ + pool_iterator->offset, 0, pool_iterator->size);
		// Add deleted pool offset to free memory list
		free_memory_.emplace_back(pool_iterator->capacity, pool_iterator->offset);
		pools_.erase(pool_iterator);
		RecompactBlockIfNeeded();
	}
	bool HasPool(PoolIndex index) const {
		return index < pools_.size() && index != INVALID_POOL_INDEX;
	}
	std::size_t GetComponentCount(PoolIndex index) const {
		assert(HasPool(index) && "Cannot check component count of nonexistent entity pool");
		return pools_[index].GetComponentCount();
	}
	// Return next matching capacity memory block in data_
	// If nothing existing found, take from end of data_
	Byte GetFreeOffset(Byte needed_capacity) {
		Byte free_offset;
		// Fetch offset from free pools first if capacities match
		for (auto it = std::begin(free_memory_); it != std::end(free_memory_);) {
			if (it->first == needed_capacity) {
				free_offset = it->second;
				it = free_memory_.erase(it);
				return free_offset;
			} else {
				++it;
			}
		}
		// If no matching capacity free pool is found, use the end of the memory block
		free_offset = size_;
		size_ += needed_capacity;
		GrowBlockIfNeeded(size_);
		return free_offset;
	}
	// Called once in manager constructor
	void CreateBlock(Byte new_capacity) {
		assert(!block_ && "Cannot call AllocateData more than once per manager");
		auto memory = static_cast<char*>(std::malloc(new_capacity));
		assert(memory && "Failed to allocate memory for manager");
		capacity_ = new_capacity;
		block_ = memory;
	}
	void GrowBlockIfNeeded(Byte size) {
		if (size > capacity_) {
			capacity_ = size * 2;
			auto memory = static_cast<char*>(std::realloc(block_, capacity_));
			assert(memory && "Failed to reallocate memory for manager");
			block_ = memory;
		}
	}
	void RecompactBlockIfNeeded() {
		if (free_memory_.size() >= FREE_POOLS_BEFORE_COMPACTING) {
			// Sort free memory by descending order of offsets so memory shifting can happen from end to beginning
			std::sort(free_memory_.begin(), free_memory_.end(), [](const std::pair<Byte, Byte> lhs, const std::pair<Byte, Byte> rhs) {
				return lhs.second > rhs.second;
			});
			Byte total_shifted_bytes = 0;
			for (auto [free_capacity, free_offset] : free_memory_) {
				// Shift each pool after free_offset back by free_capacity
				for (auto& pool : pools_) {
					if (pool.offset > free_offset) {
						pool.offset -= free_capacity;
						assert(pool.offset > 0);
					}
				}
				assert(size_ > free_offset + free_capacity);
				// Shift all memory backward starting from free offset + free_capacity up until the end of the block by free_capacity
				std::memcpy(block_ + free_offset, block_ + free_offset + free_capacity, size_ - (free_offset + free_capacity));
				total_shifted_bytes += free_capacity;
			}
			// Set end of shifted memory block to 0s
			std::memset(block_ + size_ - total_shifted_bytes, 0, total_shifted_bytes);
			free_memory_.clear();
		}
	}
	const char* GetBlock() const { return block_; }
private:
	class EntityPool {
	public:
		EntityPool() = delete;
		EntityPool(Byte offset, Byte capacity) : offset{ offset }, capacity{ capacity }, size{ 0 } {}
		Byte offset;
		Byte capacity;
		Byte size;
		Byte& AddComponentOffset(ComponentId component_id, Byte offset) {
			if (component_id >= component_offsets.size()) {
				component_offsets.resize(static_cast<std::size_t>(component_id) + 1, INVALID_COMPONENT_OFFSET);
			}
			component_offsets[component_id] = offset;
			return component_offsets[component_id];
		}
		void RemoveComponentOffset(ComponentId component_id) {
			assert(HasComponentOffset(component_id) && "Cannot remove component offset which does not exist");
			component_offsets[component_id] = INVALID_COMPONENT_OFFSET;
		}
		Byte& GetComponentOffset(ComponentId component_id) {
			assert(HasComponentOffset(component_id) && "Cannot get component offset which does not exist");
			return component_offsets[component_id];
		}
		Byte& GetComponentTracker(ComponentId component_id) {
			if (HasComponentOffset(component_id)) {
				return component_offsets[component_id];
			}
			return AddComponentOffset(component_id, INVALID_COMPONENT_OFFSET);
		}
		Byte GetComponentOffset(ComponentId component_id) const {
			assert(HasComponentOffset(component_id) && "Cannot get component offset which does not exist");
			return component_offsets[component_id];
		}
		bool HasComponentOffset(ComponentId component_id) const {
			return component_id < component_offsets.size() && component_offsets[component_id] != INVALID_COMPONENT_OFFSET;
		}
		void ShiftComponentOffsets(Byte removed_offset, Byte shift_amount) {
			for (auto& offset : component_offsets) {
				if (offset != INVALID_COMPONENT_OFFSET && offset > removed_offset) {
					offset -= shift_amount;
					assert(offset >= 0 && "Components cannot have negative offsets");
				}
			}
		}
		std::size_t GetComponentCount() const {
			std::size_t count = 0;
			for (auto& offset : component_offsets) {
				if (offset != INVALID_COMPONENT_OFFSET) {
					++count;
				}
			}
			return count;
		}
		const std::vector<Byte>& GetComponentOffsets() const { return component_offsets; }
	private:
		std::vector<Byte> component_offsets;
	};
	void MovePool(EntityPool& pool, const Byte to_capacity, const Byte to_offset) {
		assert(pool.size > 0 && "Cannot move from empty pool");
		assert(pool.capacity > 0 && "Cannot move from pool with capacity 0");
		// Copy memory block according to the to/from pool offsets
		std::memcpy(block_ + to_offset, block_ + pool.offset, pool.size);
		// Set from_pool data to 0
		std::memset(block_ + pool.offset, 0, pool.size);
		// Add old pool offset to free memory list
		free_memory_.emplace_back(pool.capacity, pool.offset);
		RecompactBlockIfNeeded();
	}
	// Returns relative byte offset of pool before size expansion // TODO: Make this comment more clear -.-
	Byte ExpandPool(EntityPool& pool, std::size_t component_size) {
		auto component_offset = pool.size;
		pool.size += component_size;
		if (pool.size > pool.capacity) {
			Byte new_capacity = pool.size * 2;
			MovePool(pool, GetFreeOffset(new_capacity), new_capacity);
		}
		return component_offset;
	}
	Byte capacity_{ 0 };
	Byte size_{ 0 };
	char* block_{ nullptr };
	// Key: capacity of block, Value: offset of block
	std::vector<std::pair<Byte, Byte>> free_memory_;
	std::vector<EntityPool> pools_;
};

template <typename T>
class Component {
public:
	Component(T* component, Byte& offset) : component_{ component }, offset_{ offset } {}
	Component(const Component&) = default;
	Component& operator=(const Component&) = default;
	Component(Component&&) = default;
	Component& operator=(Component&&) = default;
	inline const T& Get() const {
		CheckValidity(__FILE__, __LINE__);
		return *component_;
	}
	inline T& Get() {
		CheckValidity(__FILE__, __LINE__);
		return *component_;
	}
	inline const T* operator->() const {
		CheckValidity(__FILE__, __LINE__);
		return component_;
	}
	inline T* operator->() {
		CheckValidity(__FILE__, __LINE__);
		return component_;
	}
	inline friend std::ostream& operator<<(std::ostream& os, const Component obj) {
		obj.CheckValidity(__FILE__, __LINE__);
		os << obj.Get();
		return os;
	}
private:
	inline void CheckValidity(const char* file, int line) const {
		if (offset_ == INVALID_COMPONENT_OFFSET || component_ == nullptr) InvalidComponent(file, line);
	}
	void InvalidComponent(const char* file, int line) const {
		std::cerr << std::endl;
		std::cerr << "-----------------------------------------------------" << std::endl;
		std::cerr << "ECS Assertion Failed!" << std::endl;
		std::cerr << "File: " << file << std::endl;
		std::cerr << "Line: " << line << std::endl;
		std::cerr << "Component """ << typeid(T).name() << """ cannot be accessed" << std::endl;
		std::cerr << "-----------------------------------------------------" << std::endl;
		abort();
	}
	Byte& offset_;
	T* component_;
};

class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_
	Manager() : entities_{ INVALID_POOL_INDEX }, id_{ manager_count_++ } {}
	// Free memory block and call destructors on everything
	~Manager() = default;
	// Implement copying / moving of manager later
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&&) = delete;
	Manager& operator=(Manager&&) = delete;
	friend class Entity;
	// Capacity of initial entity pool allocated (in bytes)
	Entity CreateEntity(Byte entity_pool_capacity = DEFAULT_POOL_CAPACITY);
	friend inline bool operator==(const Manager& lhs, const Manager& rhs) {
		return lhs.id_ == rhs.id_;
	}
	friend inline bool operator!=(const Manager& lhs, const Manager& rhs) {
		return !(lhs == rhs);
	}
	template <typename ...Ts, typename T>
	void ForEach(T&& function, bool refresh_after_completion = true) {
		// TODO: write some tests for lambda parameters
		for (EntityId i = first_valid_entity; i <= entity_count_; ++i) {
			if (IsAlive(i) && HasComponents<Ts...>(i)) {
				function(Entity{ i, this }, (GetComponent<Ts>(i))...);
			}
		}
		if (refresh_after_completion) {
			Refresh();
		}
	}
	template <typename ...Ts, typename T>
	void ForEachWrapper(T&& function, bool refresh_after_completion = true) {
		// TODO: write some tests for lambda parameters
		for (EntityId i = first_valid_entity; i <= entity_count_; ++i) {
			if (IsAlive(i) && HasComponents<Ts...>(i)) {
				function(Entity{ i, this }, GetComponentWrapper<Ts>(i)...);
			}
		}
		if (refresh_after_completion) {
			Refresh();
		}
	}
	template <typename ...Ts, typename T>
	void ForEachTupleWrapper(T&& function, bool refresh_after_completion = true) {
		// TODO: write some tests for lambda parameters
		for (EntityId i = first_valid_entity; i <= entity_count_; ++i) {
			if (IsAlive(i) && HasComponents<Ts...>(i)) {
				function(Entity{ i, this }, GetComponentWrappers<Ts...>(i));
			}
		}
		if (refresh_after_completion) {
			Refresh();
		}
	}
	template <typename ...Ts, typename T>
	void ForEachTuple(T&& function, bool refresh_after_completion = true) {
		// TODO: write some tests for lambda parameters
		for (EntityId i = first_valid_entity; i <= entity_count_; ++i) {
			if (IsAlive(i) && HasComponents<Ts...>(i)) {
				function(Entity{ i, this }, GetComponents<Ts...>(i));
			}
		}
		if (refresh_after_completion) {
			Refresh();
		}
	}
	inline void Resize(Byte bytes, EntityId entities) {
		// Allocate at least one byte per new entity
		pool_handler_.GrowBlockIfNeeded(bytes);
		ResizeEntities(entities);
	}
	inline void RefreshDeleted() {
		for (auto id : destroyed_entities_) {
			assert(HasEntity(id) && "Cannot destroy entity which does not exist in manager");
			auto deleted_pool_index = entities_[id];
			if (deleted_pool_index != INVALID_POOL_INDEX) {
				//std::cout << "Attempting to delete pool index: " << deleted_pool_index << std::endl;
				for (auto& pool_index : entities_) {
					if (pool_index != INVALID_POOL_INDEX && pool_index > deleted_pool_index) {
						//std::cout << "Now pool_index " << pool_index << " = " << pool_index - 1 << std::endl;
						pool_index -= 1;
						assert(pool_index >= 0 && "Cannot invalidate pool index via deletion of entities");
					}
				}
				pool_handler_.FreePool(deleted_pool_index, destructors_);
				entities_[id] = INVALID_POOL_INDEX;
			}
		}
		destroyed_entities_.clear();
	}
	inline void Refresh() {
		RefreshDeleted();
		entity_changed_ = false;
	}
	inline bool HasEntityChanged() const { return entity_changed_; }
	// TODO: Make a check which only invalidates relevant caches using template comparison
	/*template <typename ...Ts>
	inline Cache<Ts...>& AddCache() {
		return *static_cast<Cache<Ts...>*>(caches_.emplace_back(std::make_unique<Cache<Ts...>>(*this)).get());
	}*/
	template <typename ...Ts>
	ComponentVector<Ts...> GetEntities();
	const char* GetPoolHandlerBlock() const { return pool_handler_.GetBlock(); }
	Entity GetEntity(EntityId id) const;
private:
	// Double entity capacity when limit is reached
	inline void GrowEntitiesIfNeeded(EntityId id) {
		if (id >= entities_.size()) {
			entities_.resize(entities_.capacity() * 2, INVALID_POOL_INDEX);
		}
	}
	inline bool HasEntity(EntityId id) const {
		return id != null && id < entities_.size();
	}
	inline bool IsAlive(EntityId id) const {
		assert(HasEntity(id) && "EntityId which is out of range cannot be alive");
		return pool_handler_.HasPool(entities_[id]);
	}
	void DestroyEntity(EntityId id) {
		assert(IsAlive(id) && "Cannot destroy nonexistent entity");
		destroyed_entities_.emplace_back(id);
		entity_changed_ = true;
	}
	inline bool HasDestructor(ComponentId component_id) const {
		return component_id < destructors_.size() && destructors_[component_id] != nullptr;
	}
	template <typename T>
	inline bool HasComponent(EntityId id) const {
		return HasComponent(id, GetComponentTypeId<T>());
	}
	inline bool HasComponent(EntityId id, ComponentId component_id) const {
		assert(IsAlive(id) && "Cannot check if nonexistent entity has component");
		auto pool_index = entities_[id];
		return pool_handler_.HasComponent(pool_index, component_id);
	}
	template <typename T>
	void RemoveComponent(EntityId id) {
		assert(IsAlive(id) && "Cannot remove component from nonexistent entity");
		auto& pool_index = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		if (HasComponent(id, component_id)) {
			auto component_location = pool_handler_.GetComponentAddress(pool_index, component_id);
			assert(component_location != nullptr && "Cannot destruct nonexistent component");
			assert(HasDestructor(component_id) && "Cannot remove component because component destructor has not been added to manager");
			// call destructor on component
			// TODO: Shrink destructors_ if this is the final component of this type in all manager instances?
			destructors_[component_id](component_location);

			auto& component_offset = pool_handler_.GetComponentOffset(pool_index, component_id);
			Byte component_size = sizeof(T);
			// This value represents all the bytes after the removed component
			auto remaining_bytes = pool_handler_.GetPoolSize(pool_index) - component_offset - component_size;

			assert(remaining_bytes >= 0 && "Cannot shift component memory block forward");

			//// Clear removed component data
			//std::memset(component_location, 0, component_size);

			// Copy remaining bytes in front of removed component backward
			std::memcpy(component_location, static_cast<char*>(component_location) + component_size, remaining_bytes);

			pool_handler_.ShiftComponentOffsets(pool_index, component_offset, component_size);
			component_offset = INVALID_COMPONENT_OFFSET;
			entity_changed_ = true;
		} else {
			// Do not waste time trying to remove component if it does not exist
		}
	}
	inline std::size_t ComponentCount(EntityId id) const {
		assert(IsAlive(id) && "Cannot check component count of nonexistent entity");
		auto pool_index = entities_[id];
		return pool_handler_.GetComponentCount(pool_index);
	}
	template <typename ...Ts>
	inline bool HasComponents(EntityId id) const {
		return (HasComponent<Ts>(id) && ...);
	}
	template <typename T, typename ...TArgs>
	inline T& ReplaceComponent(EntityId id, TArgs&&... args) {
		assert(IsAlive(id) && "Cannot replace component of nonexistent entity");
		ComponentId component_id = GetComponentTypeId<T>();
		assert(HasComponent(id, component_id) && "Cannot replace nonexistent component");
		return AddComponent<T>(id, std::forward<TArgs>(args)...);
	}
	template <typename T, typename ...TArgs>
	T& AddComponent(EntityId id, TArgs&&... args) {
		// TODO: Add static assertion to check that args is valid
		assert(IsAlive(id) && "Cannot add component to nonexistent entity");
		auto pool_index = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		void* component_location = nullptr;
		bool component_exists = HasComponent(id, component_id);
		if (component_exists) {
			component_location = pool_handler_.GetComponentAddress(pool_index, component_id);
			assert(component_location != nullptr && "Cannot replace nonexistent component");
			assert(HasDestructor(component_id) && "Cannot replace component because component destructor has not been added to manager");
			// call destructor on previous component
			destructors_[component_id](component_location);
		} else {
			component_location = pool_handler_.CreateComponentAddress(pool_index, component_id, sizeof(T));
			AddDestructor<T>(component_id);
		}
		new(component_location) T(std::forward<TArgs>(args)...);
		if (!component_exists) {
			entity_changed_ = true;
		}
		return *static_cast<T*>(component_location);
	}
	template <typename T>
	inline void AddDestructor(ComponentId component_id) {
		if (component_id >= destructors_.size()) {
			destructors_.resize(static_cast<std::size_t>(component_id) + 1, nullptr);
		}
		assert(component_id < destructors_.size() && "Failed to grow destructor vector to given component id size");
		auto destructor_function = &DestroyComponent<T>;
		assert(destructor_function != nullptr && "Component does not have a valid destructor");
		destructors_[component_id] = destructor_function;
	}
	template <typename T>
	inline T* GetComponentPointer(EntityId id) const {
		assert(HasEntity(id) && "Cannot get component pointer of nonexistent entity");
		assert(IsAlive(id) && "Cannot get component pointer of dead entity");
		auto& pool_index = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		return static_cast<T*>(pool_handler_.GetComponentAddress(pool_index, component_id));
	}
	template <typename T>
	inline Component<T> GetComponentWrapper(EntityId id) const {
		assert(HasEntity(id) && "Cannot get component wrapper for nonexistent entity");
		assert(IsAlive(id) && "Cannot get component wrapper for dead entity");
		auto& pool_index = entities_[id];
		ComponentId component_id = GetComponentTypeId<T>();
		//bool component_exists = pool_handler_.HasComponent(pool_index, component_id);
		//assert(component_exists == true && "Cannot use GetComponent without HasComponent check for nonexistent components");
		return Component<T>{ static_cast<T*>(pool_handler_.GetComponentAddress(pool_index, component_id)), pool_handler_.GetComponentTracker(pool_index, component_id) };
	}
	template <typename T>
	inline T& GetComponent(EntityId id) const {
		T* component = GetComponentPointer<T>(id);
		assert(component != nullptr && "Cannot use GetComponent without HasComponent check for nonexistent components");
		return *component;
	}
	template <typename ...Ts>
	inline std::tuple<Ts&...> GetComponents(EntityId id) const {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id)...);
	}
	template <typename ...Ts>
	inline std::tuple<Component<Ts>...> GetComponentWrappers(EntityId id) const {
		return std::forward_as_tuple<Component<Ts>...>(GetComponentWrapper<Ts>(id)...);
	}
	inline void ResizeEntities(std::size_t new_capacity) {
		if (new_capacity > entities_.capacity()) {
			entities_.resize(new_capacity, INVALID_POOL_INDEX);
		}
	}
	EntityId entity_count_{ 0 };
	std::vector<PoolIndex> entities_;
	std::vector<EntityId> destroyed_entities_;
	mutable EntityPoolHandler pool_handler_;
	// TODO: Rethink caches
	//std::vector<std::unique_ptr<BaseCache>> caches_;
	// Unique manager id
	std::size_t id_;
	// Used for refreshing manager when appropriate
	bool entity_changed_ = false;
	// Manager counter for generating unique manager ids
	static std::size_t manager_count_;
	// Components are global among all managers, therefore their destructors must be in a static vector
	// Destructor index is ComponentId of the component which it destructs
	static std::vector<destructor> destructors_;
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
	bool IsAlive() const {
		if (IsValid()) {
			return manager_->IsAlive(id_);
		}
		return false;
	}
	const EntityId GetId() const {
		assert(IsValid() && "Cannot get id of null entity");
		assert(IsAlive() && "Cannot get id of dead entity");
		return id_;
	}
	template <typename T>
	inline Component<T>& GetComponentWrapper() const {
		assert(IsValid() && "Cannot get component of null entity");
		assert(IsAlive() && "Cannot get component of dead entity");
		return manager_->GetComponentWrapper<T>(id_);
	}
	template <typename ...Ts>
	inline std::tuple<Component<Ts>...> GetComponentWrappers() const {
		assert(IsValid() && "Cannot get components of null entity");
		assert(IsAlive() && "Cannot get components of dead entity");
		return manager_->GetComponentWrappers<Ts...>(id_);
	}
	template <typename T>
	inline bool HasComponent() const {
		assert(IsValid() && "Cannot check if null entity has component");
		assert(IsAlive() && "Cannot check if dead entity has component");
		return manager_->HasComponent<T>(id_);
	}
	template <typename ...Ts>
	inline bool HasComponents() const {
		assert(IsValid() && "Cannot check if null entity has components");
		assert(IsAlive() && "Cannot check if dead entity has components");
		return manager_->HasComponents<Ts...>(id_);
	}
	template <typename T>
	inline T& GetComponent() const {
		assert(IsValid() && "Cannot get component of null entity");
		assert(IsAlive() && "Cannot get component of dead entity");
		assert(HasComponent<T>() && "Cannot GetComponent when it does not exist");
		return manager_->GetComponent<T>(id_);
	}
	template <typename ...Ts>
	inline std::tuple<Ts&...> GetComponents() const {
		assert(IsValid() && "Cannot get components of null entity");
		assert(IsAlive() && "Cannot get components of dead entity");
		assert(HasComponents<Ts...>() && "Cannot GetComponents when one of the components does not exist");
		return manager_->GetComponents<Ts...>(id_);
	}
	template <typename T, typename ...TArgs>
	inline T& AddComponent(TArgs&&... args) {
		assert(IsValid() && "Cannot add component to null entity");
		assert(IsAlive() && "Cannot add component to dead entity");
		return manager_->AddComponent<T>(id_, std::forward<TArgs>(args)...);
	}
	template <typename T, typename ...TArgs>
	inline T& ReplaceComponent(TArgs&&... args) {
		assert(IsValid() && "Cannot replace component of null entity");
		assert(IsAlive() && "Cannot replace component of dead entity");
		assert(HasComponent<T>() && "Cannot ReplaceComponent when it does not exist");
		return manager_->ReplaceComponent<T>(id_, std::forward<TArgs>(args)...);
	}
	template <typename T>
	inline void RemoveComponent() {
		assert(IsValid() && "Cannot remove component from null entity");
		assert(IsAlive() && "Cannot remove component from dead entity");
		assert(HasComponent<T>() && "Cannot RemoveComponent when it does not exist");
		manager_->RemoveComponent<T>(id_);
	}
	inline std::size_t ComponentCount() const {
		assert(IsValid() && "Cannot get component count of null entity");
		assert(IsAlive() && "Cannot get component count of dead entity");
		return manager_->ComponentCount(id_);
	}
	inline void Destroy() {
		assert(IsValid() && "Cannot destroy null entity");
		assert(IsAlive() && "Cannot destroy dead entity");
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

Entity Manager::CreateEntity(Byte entity_pool_capacity) {
	EntityId id{ ++entity_count_ };
	PoolIndex index = pool_handler_.AddPool(entity_pool_capacity);
	GrowEntitiesIfNeeded(id);
	entities_[id] = index;
	return Entity{ id, this };
}

Entity Manager::GetEntity(EntityId id) const {
	if (id == null) return Entity{ null, nullptr };
	assert(HasEntity(id) && "Cannot get entity which is out of range of manager");
	assert(IsAlive(id) && "Cannot get dead entity");
	return Entity{ id, const_cast<Manager*>(this) };
}

template <typename ...Ts>
ComponentVector<Ts...> Manager::GetEntities() {
	ComponentVector<Ts...> vector;
	for (EntityId i = first_valid_entity; i <= entity_count_; ++i) {
		if (IsAlive(i) && HasComponents<Ts...>(i)) {
			vector.emplace_back(Entity{ i, this }, GetComponent<Ts>(i)...);
		}
	}
	return vector;
}

} // namespace ecs
