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

namespace internal {

// Invalid version.
constexpr EntityVersion null_version = 0;
// Indicates a nonexistent component in a pool.
constexpr Offset INVALID_OFFSET = -1;

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
		Allocate(1);
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
	TComponent* Add(const EntityId entity, TArgs&&... args) {
		auto offset = GetFreeOffset();
		if (entity >= offsets_.size()) { // if the entity id exceeds the indexing table's size, expand the indexing table
			offsets_.resize(entity + 1, INVALID_OFFSET);
		}
		offsets_[entity] = offset;
		auto address = pool_ + offset;
		address->~TComponent();
		new(address) TComponent(std::forward<TArgs>(args)...);
		return address;
	}
	// Call the component's destructor and remove its address from the component pool.
	void Remove(const EntityId entity) {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != INVALID_OFFSET) {
				auto address = pool_ + offset;
				address->~TComponent();
				free_offsets_.emplace_back(offset);
				offset = INVALID_OFFSET;
			}
		}
	}
	// Retrieve the memory location of a component, or nullptr if it does not exist
	TComponent* Get(const EntityId entity) {
		if (Has(entity)) {
			return pool_ + offsets_[entity];
		}
		return nullptr;
	}
	// Check if the component pool contains a valid component offset for a given entity id.
	bool Has(const EntityId entity) const {
		return entity < offsets_.size() && offsets_[entity] != INVALID_OFFSET;
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
	Offset GetFreeOffset() {
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

} // namespace internal

class Entity;
class Manager;

// Component and entity registry for the ECS.
class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_ (null entity's index).
	Manager() {}
	// Invalidate manager id, reset entity count, and call destructors on everything.
	~Manager() {
		// TODO: Recheck over these.
		next_entity_id_ = 0;

		entities_.clear();
		marked_.clear();
		versions_.clear();
		pools_.clear();
		free_entity_ids_.clear();

		entities_.~vector();
		marked_.~vector();
		versions_.~vector();
		pools_.~vector();
		free_entity_ids_.~deque();
	}
	// Clear the manager, this will destroy all entities and components in the memory of the manager.
	void Clear() {
		next_entity_id_ = 0;
		entities_.clear();
		entities_.shrink_to_fit();
		marked_.clear();
		marked_.shrink_to_fit();
		versions_.clear();
		versions_.shrink_to_fit();
		for (auto pool : pools_) {
			delete pool;
			pool = nullptr;
		}
		pools_.clear();
		pools_.shrink_to_fit();
		free_entity_ids_.clear();
		free_entity_ids_.shrink_to_fit();
	}

	friend class Entity;
	// Add a new entity to the manager and return a handle to it.
	Entity CreateEntity();
	// Retrieve a vector of handles to each entity in the manager.
	std::vector<Entity> GetEntities();

	void Refresh() {
		assert(entities_.size() == versions_.size() && entities_.size() == marked_.size());
		assert(next_entity_id_ <= entities_.size());
		for (EntityId entity{ 0 }; entity < next_entity_id_; ++entity) {
			if (marked_[entity]) {
				marked_[entity] = false;
				// Entity was marked for deletion.
				if (entities_[entity]) {
					entities_[entity] = false;
					RemoveComponents(entity);
					++versions_[entity];
					free_entity_ids_.emplace_back(entity);
				} else { // Entity was marked for 'creation'.
					entities_[entity] = true;
				}
			}
		}
	}
private:
	void DestroyEntity(const EntityId entity, const EntityVersion version) {
		assert(entity < marked_.size() && entity < versions_.size());
		if (versions_[entity] == version) {
			marked_[entity] = true;
		}
	}
	bool IsAlive(const EntityId entity, const EntityVersion version) const {
		return entity < versions_.size() && versions_[entity] == version && entity < entities_.size() && entities_[entity];
	}
	// Retrieve a reference to the component pool that matches the component id, expand is necessary.
	template <typename TComponent>
	internal::Pool<TComponent>* GetPool(const ComponentId component) const {
		assert(component < pools_.size());
		return static_cast<internal::Pool<TComponent>*>(pools_[component]);
	}
	// GetComponent implementation.
	template <typename TComponent>
	const TComponent& GetComponent(const EntityId entity, const ComponentId component) const {
		const auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr);
		auto component_address = pool->Get(entity);
		assert(component_address != nullptr);
		return *component_address;
	}
	template <typename TComponent>
	TComponent& GetComponent(const EntityId entity, const ComponentId component) {
		return const_cast<TComponent&>(static_cast<const Manager&>(*this).GetComponent<TComponent>(entity, component));
	}
	template <typename TComponent>
	bool HasComponent(const EntityId entity) const {
		auto component = GetComponentId<TComponent>();
		const auto pool = GetPool<TComponent>(component);
		return pool != nullptr && pool->Has(entity);
	}
	void RemoveComponents(const EntityId entity) {
		for (auto pool : pools_) {
			if (pool) {
				pool->VirtualRemove(entity);
			}
		}
	}

	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(const EntityId entity, TArgs&&... args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>);
		static_assert(std::is_destructible_v<TComponent>);
		auto component = GetComponentId<TComponent>();
		if (component >= pools_.size()) {
			pools_.resize(component + 1);
		}
		auto pool = GetPool<TComponent>(component);
		bool new_component = pool == nullptr;
		if (new_component) {
			pool = new internal::Pool<TComponent>();
			pools_[component] = pool;
		}
		assert(pool != nullptr);
		return *pool->Add(entity, std::forward<TArgs>(args)...);
	}
	template <typename TComponent>
	void RemoveComponent(const EntityId entity) {
		auto component = GetComponentId<TComponent>();
		auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr);
		pool->Remove(entity);
	}
	// Double the entity id vector if capacity is reached.
	void GrowIfNeeded(const EntityId entity) {
		if (entity >= entities_.size()) {
			auto capacity = entities_.capacity() * 2;
			capacity = capacity == 0 ? 1 : capacity;
			assert(capacity != 0 && "Capacity is 0, cannot double size of entities_ vector");
			entities_.resize(capacity, false);
			marked_.resize(capacity, false);
			versions_.resize(capacity, internal::null_version);
		}
	}
	// Create / retrieve a unique id for each component class.
	template <typename T>
	static ComponentId GetComponentId() {
		static ComponentId id = ComponentCount()++;
		return id;
	}
	// Total entity count (dead / invalid and alive).
	EntityId next_entity_id_{ 0 };
	// Dense vector of entity ids that map to specific metadata (version and dead/alive).
	std::vector<bool> entities_;
	std::vector<bool> marked_;
	std::vector<EntityVersion> versions_;
	// Sparse vector of component pools.
	mutable std::vector<internal::BasePool*> pools_;
	// Free list of entity ids to be used before incrementing count.
	std::deque<EntityId> free_entity_ids_;
	// Important design decision: Component ids shared among all created managers, i.e. struct Position has id '3' in all manager instances, as opposed to the order in which a component is first added to each manager.
	static ComponentId& ComponentCount() { static ComponentId id{ 0 }; return id; }

};

// ECS Entity Handle
class Entity {
public:
	// Default construction to null entity.
	Entity() = default;
	// Entity construction within the manager.
	Entity(const EntityId id, const EntityVersion version, Manager* manager) : id_{ id }, version_{ version }, manager_{ manager } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	friend class Manager;

	// Add a component to the entity, returns a reference to the new component.
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(TArgs&&... constructor_args) {
		assert(IsAlive());
		return manager_->AddComponent<TComponent>(id_, std::forward<TArgs>(constructor_args)...);
	}
	template <typename TComponent>
	bool HasComponent() {
		assert(IsAlive());
		return manager_->HasComponent<TComponent>(id_);
	}
	// Remove a component from the entity.
	template <typename TComponent>
	void RemoveComponent() {
		assert(IsAlive());
		return manager_->RemoveComponent<TComponent>(id_);
	}
	// Remove all components from the entity.
	void RemoveComponents() {
		assert(IsAlive());
		return manager_->RemoveComponents(id_);
	}
	template <typename TComponent>
	TComponent& GetComponent() {
		assert(IsAlive());
		return manager_->GetComponent<TComponent>(id_, manager_->GetComponentId<TComponent>());
	}
	// Returns whether or not the entity is alive
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(id_, version_);
	}
	void Destroy() {
		assert(IsAlive());
		manager_->DestroyEntity(id_, version_);
	}

private:
	EntityId id_{ 0 };
	EntityVersion version_{ internal::null_version };
	Manager* manager_{ nullptr };
};



inline Entity Manager::CreateEntity() {
	EntityId id{ 0 };
	if (free_entity_ids_.size() > 0) { // Pick id from free list before trying to increment entity counter.
		id = free_entity_ids_.front();
		free_entity_ids_.pop_front();
	} else {
		id = next_entity_id_++;
	}
	GrowIfNeeded(id);
	assert(entities_.size() == marked_.size() && entities_.size() == versions_.size());
	assert(id < entities_.size());
	entities_[id] = false;
	marked_[id] = true;
	return Entity{ id, ++versions_[id], this };
}
inline std::vector<Entity> Manager::GetEntities() {
	std::vector<Entity> entities;
	entities.reserve(next_entity_id_);
	// Cycle through all manager entities.
	assert(entities_.size() == versions_.size());
	assert(next_entity_id_ <= entities_.size());
	for (EntityId id{ 0 }; id < next_entity_id_; ++id) {
		if (entities_[id]) {
			entities.emplace_back(id, versions_[id], this);
		}
	}
	return entities;
}

} // namespace ecs