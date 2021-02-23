/*

MIT License

Copyright (c) 2021 | Martin Starkov | https://github.com/martinstarkov

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

#include <cstdlib> // std::size_t, std::malloc, std::realloc, std::free
#include <vector> // std::vector
#include <deque> // std::deque
#include <tuple> // std::tuple
#include <type_traits> // std::enable_if, std::is_destructible_v, std::is_base_of_v, etc
#include <cassert> // assert

namespace ecs {

namespace internal {

namespace type_traits {

template <typename TComponent>
using is_valid_pool_t = std::enable_if_t<std::is_destructible_v<TComponent>, bool>;

template <typename TComponent, typename ...TArgs>
using is_valid_component_t = std::enable_if_t<std::is_constructible_v<TComponent, TArgs...>, bool>;

} // namespace type_traits

// Aliases.

using Id = std::uint32_t;
using Version = std::uint32_t;
using Offset = std::uint32_t;

// Constants.

// Represents an invalid version number.
constexpr Version null_version{ 0 };

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual BasePool* Clone() const = 0;
	virtual bool Remove(const Id entity) = 0;
	virtual void Reset() = 0;
};

template <typename TComponent, type_traits::is_valid_pool_t<TComponent> = true>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		Allocate(1);
	}
	~Pool() {
		Deallocate();
	}
	// Component pools should never be copied.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;
	// Move operator used for resizing a vector of component pools (in the manager class).
	Pool(Pool&& obj) noexcept : 
		pool_{ obj.pool_ }, 
		capacity_{ obj.capacity_ }, 
		size_{ obj.size_ }, 
		offsets_{ std::exchange(obj.offsets_, {}) }, 
		available_offsets_{ std::exchange(obj.available_offsets_, {}) } {
		obj.pool_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	Pool& operator=(Pool&&) = delete;
	/*
	* @return Pointer to an identical component pool.
	*/
	virtual BasePool* Clone() const override final {
		TComponent* new_block = nullptr;
		// Copy entire pool block over to new pool.
		std::memcpy(new_block, pool_, capacity_ * sizeof(TComponent));
		return new Pool<TComponent>(new_block, capacity_, size_, offsets_, available_offsets_);
	}
	// Resets memory block and all pool variables.
	// Equivalent to constructing an entirely new pool.
	virtual void Reset() override final {
		Deallocate();
		capacity_ = 0;
		size_ = 0;
		offsets_.clear();
		offsets_.shrink_to_fit();
		available_offsets_.clear();
		available_offsets_.shrink_to_fit();
		Allocate(1);
	}
	/* 
	* @param Id of the entity to remove a component for.
	*/
	virtual void Remove(const Id entity) override final {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != 0) {
				auto address = pool_ + (offset - 1);
				// Call destructor on component memory location.
				address->~TComponent();
				available_offsets_.emplace_back(offset);
				// Set offset to invalid.
				offset = 0;
			}
		}
	}
	/* 
	* Create / replace a component in the pool.
	* @param Id of the entity for the added component.
	* @param Arguments to be passed to the component constructor.
	* @return Pointer to the newly added / replaced component
	*/
	template <typename ...TArgs, type_traits::is_valid_component_t<TComponent, TArgs...> = true>
	TComponent* Add(const Id entity, TArgs&&... constructor_args) {
		auto offset = GetAvailableOffset();
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		if (entity >= offsets_.size()) {
			offsets_.resize(static_cast<std::size_t>(entity) + 1, 0);
		}
		offsets_[entity] = offset;
		auto address = pool_ + (offset - 1);
		// Call destructor on potential previous components
		// at the address.
		address->~TComponent();
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		return address;
	}
	/*
	* @param Id of the entity to check a component for.
	* @return True if the pool contains a valid offset, false otherwise.
	*/
	bool Has(const Id entity) const {
		return entity < offsets_.size() && offsets_[entity] != 0;
	}
	/*
	* @param Id of the entity to retrieve a component for.
	* @return The memory location of a component, nullptr if it does not exist.
	*/
	TComponent* Get(const Id entity) {
		if (Has(entity)) {
			return pool_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}
private:
	// Constructor used for cloning pools.
	Pool(const TComponent* pool, 
		 const std::size_t capacity, 
		 const std::size_t size, 
		 const std::vector<Offset>& offsets, 
		 const std::deque<Offset>& available_offsets) : 
		pool_{ pool }, 
		capacity_{ capacity }, 
		size_{ size }, 
		offsets_{ offsets }, 
		available_offsets_{ available_offsets } {}
	/*
	* Allocate some initial amount of memory for the pool.
	* @param The starting capacity of the pool (# of components it will support to begin with).
	*/
	void Allocate(const std::size_t starting_capacity) {
		assert(pool_ == nullptr && size_ == 0 && capacity_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		pool_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		assert(pool_ != nullptr && "Could not properly allocate memory for component pool");
	}
	// Destroys all the components in the offset array and frees the pool.
	void Deallocate() {
		// Call destructor of all addresses with valid offsets.
		for (auto offset : offsets_) {
			if (offset != 0) {
				auto address = pool_ + (offset - 1);
				address->~TComponent();
			}
		}
		assert(pool_ != nullptr && "Cannot free invalid component pool pointer");
		// Free the allocated pool memory block.
		std::free(pool_);
		assert(pool_ == nullptr && "Could not free component pool pointer");
	}
	/*
	* Double the capacity of a pool if the current capacity is exceeded.
	* @param New desired size of the pool (minimum # of components it should support).
	*/
	void ReallocateIfNeeded(const std::size_t new_size) {
		if (new_size >= capacity_) {
			// Double the capacity.
			capacity_ = new_size * 2;
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			pool_ = static_cast<TComponent*>(std::realloc(pool_, capacity_ * sizeof(TComponent)));
		}
	}
	/* 
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset can be generated.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset next_offset{ 0 };
		if (available_offsets_.size() > 0) {
			// Take offset from the front of the free offsets.
			// This better preserves component locality as
			// components are pooled (pun) in the front.
			next_offset = available_offsets_.front();
			available_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			next_offset = ++size_;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_offset != 0 && "Could not find a valid offset from component pool");
		return next_offset;
	}
	// Pointer to the beginning of the pool's memory block.
	TComponent* pool_{ nullptr };
	// Component capacity of the pool.
	std::size_t capacity_{ 0 };
	// Number of components currently in the pool.
	std::size_t size_{ 0 };
	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the pool_ pointer.
	std::vector<Offset> offsets_;
	// List of free offsets (avoid reallocating entire pool block).
	// Choice of double ended queue as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> available_offsets_;
};

} // namespace internal

// Forward declarations.

class Entity;
class NullEntity;

class Manager {
public:
	Manager() {
		// Reserve capacity for 1 entity so
		// size will double in powers of 2.
		Reserve(1);
	}

	~Manager() {
		DestroyPools();
	}

	// Managers cannot be copied. Use Clone() if you wish 
	// to create a new manager with identical composition.
	Manager& operator=(const Manager&) = delete;
	Manager(const Manager&) = delete;

	Manager(Manager&& obj) noexcept :
		next_entity_{ obj.next_entity_ },
		entities_{ std::exchange(obj.entities_, {}) },
		refresh_{ std::exchange(obj.refresh_, {}) },
		versions_{ std::exchange(obj.versions_, {}) },
		pools_{ std::exchange(obj.pools_, {}) },
		free_entities_{ std::exchange(obj.free_entities_, {}) } {
		obj.next_entity_ = 0;
	}
	Manager& operator=(Manager&& obj) noexcept {
		// Deallocate previous manager pools.
		DestroyPools();
		// Move manager into current manager.
		next_entity_ = obj.next_entity_;
		entities_ = std::exchange(obj.entities_, {});
		refresh_ = std::exchange(obj.refresh_, {});
		versions_ = std::exchange(obj.versions_, {});
		pools_ = std::exchange(obj.pools_, {});
		free_entities_ = std::exchange(obj.free_entities_, {});
		// Reset state of other manager.
		obj.next_entity_ = 0;
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use this in performance critical code.
	* @param Manager to compare with.
	* @return True if manager composition is identical, false otherwise.
	*/
	bool operator==(const Manager& other) const {
		return next_entity_ == other.next_entity_ 
			&& entities_ == other.entities_ 
			&& versions_ == other.versions_
			&& pools_ == other.pools_
			&& free_entities_ == other.free_entities_
			&& refresh_ == other.refresh_;
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use this in performance critical code.
	* @param Manager to compare with.
	* @return True if manager composition differs, false otherwise.
	*/
	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}

	/*
	* Copying managers accidentally is expensive.
	* This provides a way of replicating a manager with
	* identical entities and components.
	* @return New manager with identical composition.
	*/
	Manager Clone() const {
		Manager clone;
		// id_ already set to next available one inside default constructor.
		clone.next_entity_ = next_entity_;
		clone.entities_ = entities_;
		clone.refresh_ = refresh_;
		clone.versions_ = versions_;
		clone.free_entities_ = free_entities_;
		clone.pools_.resize(pools_.size(), nullptr);
		for (std::size_t i = 0; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool != nullptr) {
				clone.pools_[i] = pool->Clone();
			}
		}
		assert(clone == *this && "Cloning manager failed");
		return clone;
	}

	// Clears entity cache and reset component pools to empty ones.
	// Keeps entity capacity unchanged.
	void Clear() {
		next_entity_ = 0;

		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();

		for (auto pool : pools_) {
			if (pool != nullptr) {
				pool->Reset();
			}
		}
	}

	// Cycles through all entities and destroys 
	// ones that have been marked for destruction.
	void Refresh() {
		assert(entities_.size() == versions_.size() && entities_.size() == refresh_.size());
		assert(next_entity_ <= entities_.size());
		for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
			// Entity was marked for refresh.
			if (refresh_[entity]) {
				refresh_[entity] = false;
				if (entities_[entity]) { // Marked for deletion.
					entities_[entity] = false;
					RemoveComponents(entity);
					++versions_[entity];
					free_entities_.emplace_back(entity);
				} else { // Marked for 'creation'.
					entities_[entity] = true;
				}
			}
		}
	}

	// Clears entity cache and destroys component pools.
	// Resets entity capacity to 0.
	void Reset() {
		next_entity_ = 0;

		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();

		entities_.shrink_to_fit();
		refresh_.shrink_to_fit();
		versions_.shrink_to_fit();
		free_entities_.shrink_to_fit();

		DestroyPools();

		pools_.clear();
		pools_.shrink_to_fit();
	}

	/*
	* @return A handle to a new entity object.
	*/
	Entity CreateEntity();

	/*
	* @return A vector of handles to each living entity in the manager.
	*/
	std::vector<Entity> GetEntities();

	/*
	* @return The number of entities which are currently alive in the manager.
	*/
	std::size_t GetEntityCount() const {
		auto count = next_entity_ - GetDeadEntityCount();
		assert(count >= 0);
		return count;
	}
	/*
	* @return The number of entities which are currently not alive in the manager.
	*/
	std::size_t GetDeadEntityCount() const {
		return free_entities_.size();
	}
	/*
	* Reserve additional memory for entities.
	* @param Desired capacity of the manager. If smaller than current capacity, nothing happens.
	*/
	void Reserve(const std::size_t capacity) {
		entities_.reserve(capacity);
		refresh_.reserve(capacity);
		versions_.reserve(capacity);
		assert(entities_.capacity() == refresh_.capacity());
		// TODO: Figure out how to test this.
		//assert(versions_.capacity() == entities_.capacity());
	}
private:
	// Destroy and deallocate all the component pools.
	void DestroyPools() {
		for (auto pool : pools_) {
			delete pool;
		}
	}
	/*
	* Resize vector of entities, refresh marks and versions.
	* @param Desired size of the vectors. If smaller than current size, nothing happens.
	*/
	void Resize(const std::size_t size) {
		if (size > entities_.size()) {
			entities_.resize(size, false);
			refresh_.resize(size, false);
			versions_.resize(size, internal::null_version);
		}
		assert(entities_.size() == refresh_.size() && entities_.size() == versions_.size());
	}
	/* Destroy all components associated with an entity.
	* This requires calling a virtual Remove function
	* on each component pool.
	* @param Id of entity to remove components from.
	*/ 
	void RemoveComponents(const internal::Id entity) {
		for (auto pool : pools_) {
			if (pool) {
				pool->Remove(entity);
			}
		}
	}
	/*
	* Marks entity for deletion during next manager refresh.
	* @param Id of entity to mark for deletion.
	* @param Version of entity for handle comparison.
	*/
	void DestroyEntity(const internal::Id entity, const internal::Version version) {
		assert(entity < versions_.size() && entity < refresh_.size());
		if (versions_[entity] == version) {
			refresh_[entity] = true;
		}
	}
	/*
	* Checks if entity is valid and alive.
	* @param Id of entity to check.
	* @param Version of entity for handle comparison.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive(const internal::Id entity, const internal::Version version) const {
		return entity < versions_.size() && versions_[entity] == version && entity < entities_.size() && entities_[entity];
	}

	// Entity handles must have access to internal functions.
	// This because ids are internal and (mostly) hidden from the user.
	friend class Entity;
	// Stores the next valid entity id.
	// This will be incremented if no free id is found.
	internal::Id next_entity_{ 0 };
	// Vector index corresponds to the entity's id.
	// Element corresponds to whether or not the entity 
	// is currently alive.
	std::vector<bool> entities_;
	// Vector index corresponds to the entity's id.
	// Element corresponds to a flag for refreshing the entity.
	std::vector<bool> refresh_;
	// Vector index corresponds to the entity's id.
	// Element corresponds to the current version of the id.
	std::vector<internal::Version> versions_;
	// Vector index corresponds to the component's unique id.
	// If a component has not been added to a manager entity, 
	// its corresponding pool will be nullptr.
	std::vector<internal::BasePool*> pools_;
	// Free list of entity ids to be used 
	// before incrementing next_entity_.
	std::deque<internal::Id> free_entities_;
};

class Entity {
public:
	// Null entity constructor.
	Entity() = default;

	// Valid entity constructor.
	Entity(const internal::Id entity, const internal::Version version, Manager* manager) : entity_{ entity }, version_{ version }, manager_{ manager } {}
	
	~Entity() = default;

	// Entity handles can be moved and copied.

	Entity& operator=(const Entity&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(Entity&&) = default;
private:
	// NullEntity comparison uses versions so it requires private access.
	friend class NullEntity;
	// Id associated with an entity in the manager.
	internal::Id entity_{ 0 };
	// Version counter to check if handle has been invalidated.
	internal::Version version_{ internal::null_version };
	// Parent manager pointer for calling handle functions.
	Manager* manager_{ nullptr };
};

class NullEntity {
public:
	// Implicit conversion to entity object.
	operator Entity() const {
		return Entity{};
	}

	// Comparison to other null entities.

	constexpr bool operator==(const NullEntity&) const {
		return true;
	}
	constexpr bool operator!=(const NullEntity&) const {
		return false;
	}

	// Comparison to entity objects.

	bool operator==(const Entity& entity) const {
		return entity.version_ == internal::null_version;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}
};

// Entity comparison with null entity.

bool operator==(const Entity& entity, const NullEntity& null_entity) {
	return null_entity == entity;
}
bool operator!=(const Entity& entity, const NullEntity& null_entity) {
	return !(null_entity == entity);
}

// Null entity object.
// Allows for comparing invalid / uninitialized 
// entities with valid manager created entities.
inline constexpr NullEntity null{};

inline Entity Manager::CreateEntity() {
	internal::Id entity{ 0 };
	// Pick entity from free list before trying to increment entity counter.
	if (free_entities_.size() > 0) { 
		entity = free_entities_.front();
		free_entities_.pop_front();
	} else {
		entity = next_entity_++;
	}
	// Double the size of the manager if capacity is reached.
	if (entity >= entities_.size()) {
		Resize(entities_.capacity() * 2);
	}
	assert(entity < entities_.size());
	assert(!entities_[entity] && "Cannot create new entity from live entity");
	assert(!refresh_[entity] && "Cannot create new entity from refresh marked entity");
	// Mark entity for refresh.
	refresh_[entity] = true;
	return Entity{ entity, ++versions_[entity], this };
}

inline std::vector<Entity> Manager::GetEntities() {
	std::vector<Entity> entities;
	entities.reserve(next_entity_);
	assert(entities_.size() == versions_.size());
	assert(next_entity_ <= entities_.size());
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			entities.emplace_back(entity, versions_[entity], this);
		}
	}
	return entities;
}

} // namespace ecs