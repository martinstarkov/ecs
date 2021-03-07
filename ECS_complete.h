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
#include <cstdint> // std::uint32_t
#include <vector> // std::vector
#include <array> // std::array
#include <deque> // std::deque
#include <tuple> // std::tuple
#include <functional> // std::hash
#include <utility> // std::exchange
#include <algorithm> // std::max_element
#include <memory> // std::allocator
#include <type_traits> // std::enable_if_t, std::is_destructible_v, std::is_base_of_v, etc
#include <cassert> // assert

namespace ecs {

// Forward declarations for user-accessible types.

class Entity;
class NullEntity;
template <typename ...TComponents>
class System;
class Manager;

namespace internal {

// Forward declarations for internally used types.

class BasePool;
class BaseSystem;

// Aliases.

using Id = std::uint32_t;
using Version = std::uint32_t;
// Type representing the offset of a component 
// from the start of a memory block in a pool.
using Offset = std::uint32_t;

// Constants.

// Represents an invalid version number.
inline constexpr Version null_version{ 0 };

namespace type_traits {

template <typename TComponent>
inline constexpr bool is_valid_component_v{ 
	   std::is_destructible_v<TComponent>
	&& std::is_move_constructible_v<TComponent>
};

template <typename TComponent>
using is_valid_component = std::enable_if_t<
	is_valid_component_v<TComponent>,
	bool>;

template <typename T, typename ...TArgs>
using is_constructible = std::enable_if_t<std::is_constructible_v<T, TArgs...>, bool>;

} // namespace type_traits

class BasePool {
public:
	virtual BasePool* Clone() const = 0;
	virtual void Clear() = 0;
	virtual bool Remove(const Id entity) = 0;
	virtual std::size_t Hash() const = 0;
};

class BaseSystem {
public:

protected:

private:

};

/* 
* Container which stores the offsets of components
* from the beginning of a memory block pointer.
* Offsets can be accessed using unique entity ids.
* Component must be move constructible and destructible.
* @tparam TComponent Type of component to store in the pool.
*/
template <typename TComponent, 
	type_traits::is_valid_component<TComponent> = true>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		// This enables capacity to double in powers of 2.
		AllocateMemoryBlock(1);
	}

	~Pool() {
		CallComponentDestructors();
		FreeMemoryBlock();
		assert(block_ == nullptr && "Pool memory must be freed before pool destruction");
	}

	// Component pools should never be copied.
	// Use Clone() instead.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	// Move operator used when resizing a vector 
	// of component pools (in the manager class).
	Pool(Pool&& obj) noexcept :
		block_{ obj.block_ },
		capacity_{ obj.capacity_ },
		size_{ obj.size_ },
		offsets_{ std::exchange(obj.offsets_, {}) },
		freed_offsets_{ std::exchange(obj.freed_offsets_, {}) } {
		obj.block_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	
	// Pools should never be move assigned.
	Pool& operator=(Pool&&) = delete;
	
	/*
	* Creates a duplicate (copy) pool with
	* all the same components and offsets.
	* @return Pointer to an identical component pool.
	*/
	virtual BasePool* Clone() const override final {
		static_assert(std::is_copy_constructible_v<TComponent>, 
					  "Cannot clone pool with a non copy constructible component");
		// Empty memory block for clone is allocated in constructor.
		auto clone = new Pool<TComponent>(
			capacity_, 
			size_, 
			offsets_, 
			freed_offsets_
		);
		// Copy entire pool block over to new pool block.
		for (auto offset : offsets_) {
			if (offset != 0) {
				TComponent* component = block_ + (offset - 1);
				TComponent* address = clone->block_ + (offset - 1);
				// Copy component from current pool to 
				// other pool using copy constructor.
				new(address) TComponent{ *component };
			}
		}
		return clone;
	}

	// Calls destructor on each component.
	// Clears free and occupied offsets.
	// Does not modify pool capacity.
	// Equivalent to clearing a vector.
	virtual void Clear() override final {
		CallComponentDestructors();
		offsets_.clear();
		freed_offsets_.clear();
		size_ = 0;
	}

	/*
	* Removes the component from the given entity.
	* @param entity Id of the entity to remove a component from.
	* @return True if component was removed, false otherwise.
	*/
	virtual bool Remove(const Id entity) override final {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != 0) {
				TComponent* address = block_ + (offset - 1);
				// Call destructor on component memory location.
				address->~TComponent();
				freed_offsets_.emplace_back(offset);
				// Set offset to invalid.
				offset = 0;
				return true;
			}
		}
		return false;
	}

	/*
	* Creates / replaces a component for an entity in the pool.
	* Component must be constructible from the given arguments.
	* @tparam TArgs Types of constructor arguments.
	* @param entity Id of the entity to add a component to.
	* @param constructor_args Arguments to be passed to the component constructor.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename ...TArgs, 
		type_traits::is_constructible<TComponent, TArgs...> = true>
	TComponent& Add(const Id entity, TArgs&&... constructor_args) {
		auto offset = GetAvailableOffset();
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		if (static_cast<std::size_t>(entity) >= offsets_.size()) {
			offsets_.resize(static_cast<std::size_t>(entity) + 1, 0);
		}
		// Check if component offset exists already.
		bool replace = offsets_[entity] != 0;
		offsets_[entity] = offset;
		TComponent* address = block_ + (offset - 1);
		if (replace) {
			// Call destructor on potential previous components
			// at the address.
			address->~TComponent();
		}
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		assert(address != nullptr && "Failed to create component at offset memory location");
		return *address;
	}

	/*
	* Checks if the entity has a component in the pool.
	* @param entity Id of the entity to check a component for.
	* @return True if the pool contains a valid component offset, false otherwise.
	*/
	bool Has(const Id entity) const {
		return entity < offsets_.size() && offsets_[entity] != 0;
	}

	/*
	* Retrieves the component of a given entity from the pool.
	* @param entity Id of the entity to retrieve a component for.
	* @return Pointer to the component, nullptr if the component does not exist.
	*/
	TComponent* Get(const Id entity) {
		if (Has(entity)) {
			return block_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}

	/*
	* Generates a hash number using pool members.
	* Useful for identifying if two pools are identical.
	* @return Hash code for the pool.
	*/
	virtual std::size_t Hash() const override final {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<Offset>()(size_);
		h = h * 31 + std::hash<Offset>()(capacity_);
		// Modified container hashing from:
		// https://stackoverflow.com/a/27216842
		auto container_hash = [](auto& v) {
			std::size_t seed = v.size();
			for (auto i : v) {
				seed ^= static_cast<std::size_t>(i) 
					+ 0x9e3779b9 
					+ (seed << 6) 
					+ (seed >> 2);
			}
			return seed;
		};
		h = h * 31 + container_hash(offsets_);
		h = h * 31 + container_hash(freed_offsets_);
		return h;
	}

private:
	// Constructor used for cloning identical pools.
	Pool(const Offset capacity,
		 const Offset size,
		 const std::vector<Offset>& offsets,
		 const std::deque<Offset>& freed_offsets) : 
		// Allocate memory block before capacity is set 
		// as otherwise capacity == 0 assertion fails.
		capacity_{ (AllocateMemoryBlock(capacity), capacity) },
		size_{ size },
		offsets_{ offsets },
		freed_offsets_{ freed_offsets } {
	}

	/*
	* Allocate an initial memory block for the pool.
	* @param starting_capacity The starting capacity of the pool.
	* (number of components it should support to begin with).
	*/
	void AllocateMemoryBlock(const Offset starting_capacity) {
		assert(block_ == nullptr);
		assert(capacity_ == 0);
		assert(size_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		block_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		assert(block_ != nullptr && "Could not properly allocate memory for component pool");
	}

	// Invokes the destructor of each valid component in the pool.
	// Note: valid offsets are not refreshed afterward.
	void CallComponentDestructors() {
		for (auto offset : offsets_) {
			// Only consider valid offsets.
			if (offset != 0) {
				TComponent* address = block_ + (offset - 1);
				address->~TComponent();
			}
		}
	}

	// Frees the allocated memory block associated with the pool.
	// This must be called whenever destroying a pool.
	void FreeMemoryBlock() {
		assert(block_ != nullptr && "Cannot free invalid component pool pointer");
		std::free(block_);
		block_ = nullptr;
	}

	/*
	* Doubles the capacity of a pool if the current capacity is exceeded.
	* @param new_size Desired size of the pool.
	* (minimum number of components it should support).
	*/
	void ReallocateIfNeeded(const Offset new_size) {
		if (new_size >= capacity_) {
			// Double the capacity.
			capacity_ = new_size * 2;
			assert(block_ != nullptr && "Pool memory must be allocated before reallocation");
			block_ = static_cast<TComponent*>(std::realloc(block_, capacity_ * sizeof(TComponent)));
		}
	}
	
	/*
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset is available.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset next_offset{ 0 };
		if (freed_offsets_.size() > 0) {
			// Take offset from the front of the free offsets.
			// This better preserves component locality as
			// components are pooled (pun) in the front.
			next_offset = freed_offsets_.front();
			freed_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			// Offset of 0 is considered invalid.
			// In each access case 1 is subtracted so 0th
			// offset is still used.
			next_offset = ++size_;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_offset != 0 && "Could not find a valid offset from component pool");
		return next_offset;
	}

	// Pointer to the beginning of the pool's memory block.
	TComponent* block_{ nullptr };

	// Component capacity of the pool.
	Offset capacity_{ 0 };

	// Number of components currently in the pool.
	Offset size_{ 0 };

	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the block_ pointer.
	std::vector<Offset> offsets_;

	// Queue of free offsets (avoids frequent reallocation of pools).
	// Double ended queue is chosen as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> freed_offsets_;
};

} // namespace internal

class Entity {
public:
	// Null entity constructor.
	Entity() = default;

	// Valid entity constructor.
	// This is not intended for use by users of the ECS library.
	// It must remain public in order to allow vector emplace construction.
	Entity(const internal::Id entity, const internal::Version version, Manager* manager) : entity_{ entity }, version_{ version }, manager_{ manager } {}

	~Entity() = default;

	// Entity handles can be moved and copied.

	Entity& operator=(const Entity&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(Entity&&) = default;

	// Entity handle comparison operators.

	bool operator==(const Entity& entity) const {
		return entity_ == entity.entity_
			&& version_ == entity.version_
			&& manager_ == entity.manager_;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}

	/*
	* Retrieves const reference to the parent manager of the entity.
	* @return Const reference to the parent manager.
	*/
	const Manager& GetManager() const {
		assert(manager_ != nullptr && "Cannot return parent manager of a null entity");
		return *manager_;
	}

	/*
	* Retrieves reference to the parent manager of the entity.
	* @return Reference to the parent manager.
	*/
	Manager& GetManager() {
		return const_cast<Manager&>(static_cast<const Entity&>(*this).GetManager());
	}

	/*
	* Checks if entity handle is valid and alive.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(entity_, version_);
	}

	/*
	* Retrieves a const reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component.
	* @return Const reference to the component.
	*/
	template <typename TComponent>
	const TComponent& GetComponent() const {
		assert(IsAlive() && "Cannot retrieve component for dead or null entity");
		return manager_->GetComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Retrieves a reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component.
	* @return Reference to the component.
	*/
	template <typename TComponent>
	TComponent& GetComponent() {
		return const_cast<TComponent&>(static_cast<const Entity&>(*this).GetComponent<TComponent>());
	}

	template <typename ...TComponents>
	std::tuple<const TComponents&...> GetComponents() const {
		return std::forward_as_tuple<const TComponents&...>(GetComponent<TComponents>()...);
	}

	template <typename ...TComponents>
	std::tuple<TComponents&...> GetComponents() {
		return std::forward_as_tuple<TComponents&...>(GetComponent<TComponents>()...);
	}

	/*
	* Adds a component to the entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param constructor_args Arguments for constructing component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>, "Cannot construct component type from given arguments");
		static_assert(std::is_destructible_v<TComponent>, "Cannot add component which does not have a valid destructor");
		assert(IsAlive() && "Cannot add component to dead or null entity");
		return manager_->AddComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>(), std::forward<TArgs>(constructor_args)...);
	}

	/*
	* Checks if entity has a component.
	* @tparam TComponent Type of component to check.
	* @return True is entity has component, false otherwise.
	*/
	template <typename TComponent>
	bool HasComponent() const {
		assert(IsAlive() && "Cannot check if dead or null entity has a component");
		return manager_->HasComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Check if entity has all the given components.
	* @tparam TComponents Types of components to check.
	* @return True if entity has each component, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents() const {
		assert(IsAlive() && "Cannot check if dead or null entity has components");
		return manager_->HasComponents<TComponents...>(entity_);
	}

	/*
	* Removes a component from the entity.
	* If entity does not have component type, nothing happens.
	* @tparam TComponent Type of component to remove.
	*/
	template <typename TComponent>
	void RemoveComponent() {
		assert(IsAlive() && "Cannot remove component from dead or null entity");
		manager_->RemoveComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	template <typename ...TComponents>
	bool RemoveComponents() const {
		assert(IsAlive() && "Cannot remove components from dead or null entity");
		return manager_->RemoveComponents<TComponents...>(entity_);
	}

	/*
	* Removes all components from the entity.
	*/
	void RemoveComponents() {
		assert(IsAlive() && "Cannot remove all components from dead or null entity");
		return manager_->RemoveComponents(entity_);
	}

	// Marks the entity for destruction.
	// Note that the entity will remain alive and valid 
	// until Refresh() is called on its parent manager.
	// If entity is already marked for deletion, nothing happens.
	void Destroy() {
		if (IsAlive()) {
			manager_->DestroyEntity(entity_, version_);
		}
	}
private:

	internal::Id GetId() const {
		return entity_;
	}

	internal::Version GetVersion() const {
		return version_;
	}

	friend struct std::hash<Entity>;

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

	// Constexpr comparisons of null entities to each other.

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

inline bool operator==(const Entity& entity, const NullEntity& null_entity) {
	return null_entity == entity;
}
inline bool operator!=(const Entity& entity, const NullEntity& null_entity) {
	return !(null_entity == entity);
}

// Null entity object.
// Allows for comparing invalid / uninitialized 
// entities with valid manager created entities.
inline constexpr NullEntity null{};

template <typename ...TRequiredComponents>
class System {
public:

protected:

private:

};

class Manager {
public:
	std::vector<Entity> GetEntities() {
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
private:

	/*
	* Return a unique id for a type of component.
	* @tparam TComponent Type of component.
	* @return Unique id for the given component type.
	*/
	template <typename TComponent>
	static internal::Id GetComponentId() {
		// Get the next available id save that id as
		// a static variable for the component type.
		static internal::Id id{ ComponentCount()++ };
		return id;
	}
	/*
	* Important design decision: Component ids shared among all created managers
	* I.e. struct Position has id '3' in all manager instances, as opposed to
	* the order in which a component is first added to each manager.
	* @return Next available component id.
	*/
	static internal::Id& ComponentCount() {
		static internal::Id id{ 0 };
		return id;
	}

	/*
	* Return a unique id for a type of system.
	* @tparam TSystem Type of system.
	* @return Unique id for the given system type.
	*/
	template <typename TSystem>
	static internal::Id GetSystemId() {
		// Get the next available id save that id as
		// a static variable for the system type.
		static internal::Id id{ SystemCount()++ };
		return id;
	}
	/*
	* Important design decision: System ids shared among all created managers
	* I.e. class HealthSystem has id '3' in all manager instances, as opposed to
	* the order in which a system is first added to each manager.
	* @return Next available system id.
	*/
	static internal::Id& SystemCount() {
		static internal::Id id{ 0 };
		return id;
	}

	// Entity handles must have access to internal functions.
	// This because ids are internal and (mostly) hidden from the user.
	friend class Entity;

	template <typename ...TComponents>
	friend class System;

	// Stores the next valid entity id.
	// This will be incremented if no free id is found.
	internal::Id next_entity_{ 0 };

	// Stores the current alive entity count (purely for retrieval).
	internal::Id count_{ 0 };

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
	// its corresponding pool pointer will be nullptr.
	// Mutable as pools are accessed from const methods.
	mutable std::vector<internal::BasePool*> pools_;

	// Vector index corresponds to the system's unique id.
	// If a system has not been added to a manager entity, 
	// its corresponding pointer will be nullptr.
	std::vector<internal::BaseSystem*> systems_;

	// Free list of entity ids to be used 
	// before incrementing next_entity_.
	std::deque<internal::Id> free_entities_;
};



} // namespace ecs
