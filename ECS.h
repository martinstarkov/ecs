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

// Type representing the identifier of each
// entity within the manager. 
// This id is used to index all internal storage vectors.
using Id = std::uint32_t;
// Type representing how many times an entity id
// has been reused in the manager.
// Incremented each time an entity is fully destroyed.
// Allows for reuse of entity ids.
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

// Source: https://stackoverflow.com/a/34672753/4384023
template <template <typename...> class base, typename derived>
struct is_base_of_template_impl {
	template<typename... Ts>
	static constexpr std::true_type  test(const base<Ts...>*);
	static constexpr std::false_type test(...);
	using type = decltype(test(std::declval<derived*>()));
};

template < template <typename...> class base, typename derived>
using is_base_of_template = typename is_base_of_template_impl<base, derived>::type;

template <typename TSystem>
inline constexpr bool is_system_v{ is_base_of_template<System, TSystem>::value };

template <typename TSystem>
using is_valid_system = std::enable_if_t<is_system_v<TSystem>, bool>;

} // namespace type_traits

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual BasePool* Clone() const = 0;
	virtual void Clear() = 0;
	virtual bool Remove(const Id entity) = 0;
	virtual std::size_t Hash() const = 0;
};

class BaseSystem {
public:
	virtual ~BaseSystem() = default;
	virtual void Update() = 0;
private:
	friend class Manager;
	virtual void SetManager(Manager* manager) = 0;
	virtual void SetComponentDependencies() = 0;
	virtual void FlagIfDependsOnNone() = 0;
	virtual BaseSystem* Clone() const = 0;
	virtual void ResetCacheIfFlagged() = 0;
	virtual void FlagIfDependsOn(const internal::Id component) = 0;
	virtual void FlagForReset() = 0;
	virtual std::size_t Hash() const = 0;
};

/*
* Container which stores the offsets of components from
* the beginning of a memory block pointer.
* Offsets can be accessed using unique entity ids.
* Component must be move constructible and destructible.
* @tparam TComponent Type of component to store in the pool.
* If no types are given, system will fetch all manager entities.
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
		* @return Const pointer to the component, nullptr if the component does not exist.
		*/
		const TComponent* Get(const Id entity) const {
			if (Has(entity)) {
				return block_ + (offsets_[entity] - 1);
			}
			return nullptr;
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

/*
* Template class for ECS systems.
* @tparam TRequiredComponents Types of components required by entities in the system.
* Each system has access to a protected 'entities' variable.
* Example:
* struct MySystem : public System<Transform, RigidBody>;
* 'entities' can be used inside the system's methods like so:
* auto [auto entity, transform, rigid_body] = entities[0];
* or using a loop:
* for (auto [entity, transform, rigid_body] : entities)
*	... use handle / component references here ...
* If a system-required component is removed from an entity,
* that entity will be removed from 'entities' before the next update call.
*/
template <typename ...TRequiredComponents>
class System : public internal::BaseSystem {
public:
	System() = default;
	virtual ~System() = default;

protected:

	// Function called by manager's UpdateSystem() method.
	// Override this for customizeable system logic.
	virtual void Update() override {}

	/*
	* Retrieves the parent manager of the system.
	* @return Reference to the manager the system is a part of.
	*/
	Manager& GetManager() {
		assert(manager_ != nullptr && "Cannot retrieve manager for uninitialized system");
		return *manager_;
	}

	// A vector of tuples where the first tuple element is a copy of
	// an entity handle and the rest are references to that entities' 
	// components as determined by the system's required components.
	// Entities are cached in the system and the cache is automatically
	// updated, which allows for fast traversal.
	std::vector<std::tuple<Entity, TRequiredComponents&...>> entities;
private:
	// Manager requires private access for processing and manipulating systems.
	friend class ecs::Manager;

	/*
	* Generates a hash number using system members.
	* Useful for identifying if two systems are identical.
	* @return Hash code for the system.
	*/
	virtual std::size_t Hash() const override final {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<Manager*>()(manager_);
		h = h * 31 + std::hash<bool>()(reset_required_);
		h = h * 31 + std::hash<std::vector<bool>>()(components_);
		return h;
	}

	/*
	* Creates a duplicate (copy) system with
	* all the same component dependencies.
	* Cache reset is automatically queued as
	* an entity cache does not exist yet.
	* @return Pointer to a new identical system.
	*/
	virtual internal::BaseSystem* Clone() const override final {
		return new System<TRequiredComponents...>(manager_, components_);
	}

	// Constructor for creating an identical system (when cloning).
	System(Manager* manager,
		   const std::vector<bool>& components) :
		manager_{ manager },
		components_{ components },
		reset_required_{ true } {
	}

	/*
	* Initializes the internal pointer to the
	* parent manager of the system.
	* This function is always called internally
	* when creating a system inside the manager.
	* @param manager Pointer to the parent manager of the system.
	*/
	virtual void SetManager(Manager* manager) override final {
		assert(manager != nullptr && "Cannot set system manager to nullptr");
		manager_ = manager;
	}

	// Flags the system for a cache reset for the next update cycle.
	virtual void FlagForReset() override final {
		reset_required_ = true;
	}

	/*
	* Flags the system for a cache reset if it depends on the given
	* component id.
	* @param component Id of the component to check system dependency for.
	*/
	virtual void FlagIfDependsOn(const internal::Id component) override final {
		if (component < components_.size() && components_[component]) {
			reset_required_ = true;
		}
	}

	// Flags the system for a cache reset if the system requires 
	// no specific components. This is required for invalidating
	// system caches when a user has created a new entity and
	// calls Refresh() on the manager.
	virtual void FlagIfDependsOnNone() override final {
		if constexpr (sizeof...(TRequiredComponents) == 0) {
			reset_required_ = true;
		}
	}

	// Checks if the system is flagged for a cache reset.
	// If flagged, this function fetches a new 'entities'
	// vector with relevant entities (i.e. resets cache).
	virtual void ResetCacheIfFlagged() override final;

	// Populates the system's component bitset with the
	// template components' ids. Bitset is empty if the
	// system requires no specific components (all entities).
	virtual void SetComponentDependencies() override final;

	// Pointer to the parent manager of the system.
	// Set internally in the manager AddSystem() function.
	Manager* manager_{ nullptr };

	// Bitset where the indexes corresponds to component ids
	// and the booleans indicate whether or not the system
	// requires the given component type.
	std::vector<bool> components_;

	// Flag for when the cache becomes invalid and must be reset.
	bool reset_required_{ false };
};

// Entity and component storage class for the ECS.
class Manager {
public:
	Manager() {
		// Reserve capacity for 1 entity so that
		// manager size will double in powers of 2.
		Reserve(1);
	}

	~Manager() {
		DestroySystems();
		DestroyPools();
	}

	// Managers cannot be copied. Use Clone() if you wish 
	// to create a new manager with identical composition.
	Manager& operator=(const Manager&) = delete;
	Manager(const Manager&) = delete;

	Manager(Manager&& obj) noexcept :
		next_entity_{ obj.next_entity_ },
		refresh_required_{ obj.refresh_required_ },
		entities_{ std::exchange(obj.entities_, {}) },
		refresh_{ std::exchange(obj.refresh_, {}) },
		versions_{ std::exchange(obj.versions_, {}) },
		pools_{ std::exchange(obj.pools_, {}) },
		systems_{ std::exchange(obj.systems_, {}) },
		free_entities_{ std::exchange(obj.free_entities_, {}) } {
		obj.next_entity_ = 0;
	}

	Manager& operator=(Manager&& obj) noexcept {
		// Destroy previous manager internals.
		DestroySystems();
		DestroyPools();
		// Move manager into current manager.
		next_entity_ = obj.next_entity_;
		refresh_required_ = obj.refresh_required_;
		entities_ = std::exchange(obj.entities_, {});
		refresh_ = std::exchange(obj.refresh_, {});
		versions_ = std::exchange(obj.versions_, {});
		pools_ = std::exchange(obj.pools_, {});
		systems_ = std::exchange(obj.systems_, {});
		free_entities_ = std::exchange(obj.free_entities_, {});
		// Reset state of other manager.
		obj.next_entity_ = 0;
		return *this;
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use comparison in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition is identical, false otherwise.
	*/
	bool operator==(const Manager& other) const {
		return next_entity_ == other.next_entity_
			&& refresh_required_ == other.refresh_required_
			&& entities_ == other.entities_
			&& versions_ == other.versions_
			&& free_entities_ == other.free_entities_
			&& refresh_ == other.refresh_
			// Compare manager systems.
			&& std::equal(std::begin(systems_), std::end(systems_),
						  std::begin(other.systems_), std::end(other.systems_),
						  [](const internal::BaseSystem* lhs, const internal::BaseSystem* rhs) {
			return lhs && rhs && lhs->Hash() == rhs->Hash();
		})
			// Compare manager component pools.
			&& std::equal(std::begin(pools_), std::end(pools_),
						  std::begin(other.pools_), std::end(other.pools_),
						  [](const internal::BasePool* lhs, const internal::BasePool* rhs) {
			return lhs && rhs && lhs->Hash() == rhs->Hash();
		});
	}

	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use comparison in performance critical code.
	* @param other Manager to compare with.
	* @return True if manager composition differs in any way, false otherwise.
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
		clone.next_entity_ = next_entity_;
		clone.entities_ = entities_;
		clone.refresh_ = refresh_;
		clone.refresh_required_ = refresh_required_;
		clone.versions_ = versions_;
		clone.free_entities_ = free_entities_;
		clone.pools_.reserve(pools_.size());
		clone.pools_.resize(pools_.size(), nullptr);
		for (std::size_t i = 0; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool != nullptr) {
				// Clone pools over to new manager.
				clone.pools_[i] = pool->Clone();
			}
		}
		clone.systems_.reserve(systems_.size());
		clone.systems_.resize(systems_.size(), nullptr);
		for (std::size_t i = 0; i < systems_.size(); ++i) {
			auto system = systems_[i];
			if (system != nullptr) {
				// Clone systems over to new manager.
				clone.systems_[i] = system->Clone();
			}
		}
		assert(clone == *this && "Cloning manager failed");
		return clone;
	}

	// Clears entity cache and reset component pools to empty ones.
	// Keeps entity capacity unchanged.
	// Systems are not removed but caches are flagged for reset.
	void Clear() {
		next_entity_ = 0;
		refresh_required_ = false;

		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();

		for (auto pool : pools_) {
			if (pool != nullptr) {
				pool->Clear();
			}
		}
		for (auto system : systems_) {
			if (system != nullptr) {
				system->FlagForReset();
			}
		}
	}

	// Cycles through all entities and destroys 
	// ones that have been marked for destruction.
	// Activates created entities (can be used in systems).
	void Refresh() {
		if (refresh_required_) {
			refresh_required_ = false;
			assert(entities_.size() == versions_.size());
			assert(entities_.size() == refresh_.size());
			assert(next_entity_ <= entities_.size());
			internal::Id alive{ 0 };
			internal::Id dead{ 0 };
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				// Entity was marked for refresh.
				if (refresh_[entity]) {
					refresh_[entity] = false;
					if (entities_[entity]) { // Marked for deletion.
						RemoveComponents(entity);
						entities_[entity] = false;
						++versions_[entity];
						free_entities_.emplace_back(entity);
						++dead;
					} else { // Marked for 'creation'.
						entities_[entity] = true;
						++alive;
					}
				}
			}
			if (alive > 0) {
				// If even a single entity was created, all caches become
				// invalid as that entity could have had components added to it. 
				// Since Refresh() call cannot be guaranteed after one cycle, 
				// caches would otherwise miss some entities.
				for (auto system : systems_) {
					if (system) {
						system->FlagForReset();
					}
				}
			} else if (dead > 0) {
				// If entities were only destroyed during refresh,
				// flag systems which don't depend on components 
				// (i.e. systems which cache all manager entities).
				for (auto system : systems_) {
					if (system) {
						system->FlagIfDependsOnNone();
					}
				}
			}
			assert(alive >= 0 && dead >= 0);
			// Update entity count with net change.
			count_ += alive - dead;
			assert(count_ >= 0);
		}
	}

	/*
	* Reserve additional memory for manager entities.
	* @param capacity Desired capacity of the manager.
	* If smaller than current capacity, nothing happens.
	*/
	void Reserve(const std::size_t capacity) {
		entities_.reserve(capacity);
		refresh_.reserve(capacity);
		versions_.reserve(capacity);
		assert(entities_.capacity() == refresh_.capacity());
	}

	// Equivalent to creating an entirely new manager.
	// Resets entity caches and destroys component pools.
	// Resets all capacities to 0.
	void Reset() {
		next_entity_ = 0;
		refresh_required_ = 0;

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

		DestroySystems();
		systems_.clear();
		systems_.shrink_to_fit();

		Reserve(1);
	}

	/*
	* Adds a new entity to the manager and creates a handle for it.
	* @return Handle to newly created entity.
	*/
	Entity CreateEntity();

	/*
	* Retrieves all valid entities in the manager.
	* @return A vector of handles to each living entity in the manager.
	*/
	std::vector<Entity> GetEntities();

	/*
	* Retrieves entities with the given component types.
	* @tparam TComponents Component types required for each returned entity.
	* @return A vector of handles to each matching entity.
	*/
	template <typename ...TComponents>
	std::vector<Entity> GetEntitiesWith();

	/*
	* Retrieves entities without the given component types.
	* @tparam TComponents Component types to ignore entities with.
	* @return A vector of handles to entities without the given components.
	*/
	template <typename ...TComponents>
	std::vector<Entity> GetEntitiesWithout();

	// Marks every single entity in the manager for destruction.
	// Requires a Refresh() call to destroy entities and their components.
	void DestroyEntities();

	/*
	* Marks all entities with the given component types for destruction.
	* Requires a Refresh() call to destroy entities and their components.
	* @tparam TComponents Component types required for marking entity.
	*/
	template <typename ...TComponents>
	void DestroyEntitiesWith();

	/*
	* Marks all entities without the given component types for destruction.
	* Requires a Refresh() call to destroy entities and their components.
	* @tparam TComponents Component types required for ignoring an entity.
	*/
	template <typename ...TComponents>
	void DestroyEntitiesWithout();

	/*
	* Fetches a vector of tuples where the first element
	* is an entity and the rest are the requested components.
	* Only retrieves entities which have each component.
	* @return Vector of tuples of entity handles and components.
	*/
	template <typename ...TComponents>
	std::vector<std::tuple<Entity, TComponents&...>> GetEntityComponents();

	/*
	* @return The number of entities currently alive in the manager.
	*/
	std::size_t GetEntityCount() const {
		return count_;
	}

	/*
	* @return The number of entities currently not alive in the manager.
	*/
	std::size_t GetDeadEntityCount() const {
		return free_entities_.size();
	}

	/*
	* Adds a system to the manager, it is exists it will be replaced.
	* Constructor arguments are not supported as systems are purely logic.
	* @tparam TSystem Type of system to add to the manager.
	*/
	template <typename TSystem>
	void AddSystem() {
		static_assert(internal::type_traits::is_system_v<TSystem>,
					  "Cannot add a system to the manager which does not inherit from ecs::System class");
		AddSystemImpl<TSystem>();
	}

	/*
	* Update a system inside the manager.
	* @tparam TSystem Type of system to update.
	*/
	template <typename TSystem>
	void UpdateSystem() {
		static_assert(internal::type_traits::is_system_v<TSystem>,
					  "Cannot update a system which does not inherit from ecs::System class");
		assert(HasSystem<TSystem>() && "Cannot update a system which does not exist in the manager");
		UpdateSystemImpl<TSystem>();
	}

	/*
	* Checks if the given system has been added to the manager.
	* @tparam TSystem Type of system to check.
	* @return True if manager has the system, false otherwise.
	*/
	template <typename TSystem>
	bool HasSystem() const {
		// Exit eaerly in compile-time if given system
		// does not inherit from the ecs::System class.
		if constexpr (!internal::type_traits::is_system_v<TSystem>) {
			return false;
		} else {
			auto system = GetSystemId<TSystem>();
			return system < systems_.size() && systems_[system] != nullptr;
		}
	}

	/*
	* Removes a system from the manager.
	* @tparam TSystem Type of system to remove.
	*/
	template <typename TSystem>
	void RemoveSystem() {
		if constexpr (internal::type_traits::is_system_v<TSystem>) {
			auto system = GetSystemId<TSystem>();
			if (system < systems_.size()) {
				delete systems_[system];
			}
		}
	}

private:
	// Destroys and deallocates all the component pools.
	void DestroyPools() {
		for (auto pool : pools_) {
			delete pool;
		}
	}

	// Destroys all manager systems.
	void DestroySystems() {
		for (auto system : systems_) {
			delete system;
		}
	}

	/*
	* Resize vector of entities, refresh flags, and versions.
	* If smaller than current size, nothing happens.
	* @param size Desired size of the vectors.
	*/
	void Resize(const std::size_t size) {
		if (size > entities_.size()) {
			Reserve(size);
			entities_.resize(size, false);
			refresh_.resize(size, false);
			versions_.resize(size, internal::null_version);
		}
		assert(entities_.size() == refresh_.size());
		assert(entities_.size() == versions_.size());
	}

	/*
	* Marks entity for deletion for next manager refresh.
	* @param entity Id of entity to mark for deletion.
	* @param version Version of entity for handle comparison.
	*/
	void DestroyEntity(const internal::Id entity, const internal::Version version) {
		assert(entity < versions_.size());
		assert(entity < refresh_.size());
		if (versions_[entity] == version) {
			if (!refresh_[entity] || entities_[entity]) {
				refresh_[entity] = true;
				refresh_required_ = true;
			} else {
				// Edge case where entity is created and marked 
				// for deletion before a Refresh() has been called.
				// In this case, destroy and invalidate the entity 
				// without a Refresh() call. This is equivalent to
				// an entity which never 'officially' existed in the manager.
				RemoveComponents(entity);
				refresh_[entity] = false;
				++versions_[entity];
				free_entities_.emplace_back(entity);
			}
		}
	}

	/*
	* Checks if entity is alive in the manager.
	* Newly added entities are considered alive.
	* @param entity Id of entity to check.
	* @param version Version of entity for handle comparison.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive(const internal::Id entity, const internal::Version version) const {
		return entity < versions_.size()
			&& versions_[entity] == version
			&& entity < entities_.size()
			&& (entities_[entity] || refresh_[entity]);
	}

	/*
	* Retrieves a pointer to the component pool with matching component id.
	* @tparam TComponent Type of component to retrieve pool for.
	* @param component Id of component to retrieve pool for.
	* @return Pointer to the component pool, nullptr if pool does not exist.
	*/
	template <typename TComponent>
	internal::Pool<TComponent>* GetPool(const internal::Id component) const {
		assert(component == GetComponentId<TComponent>());
		if (component < pools_.size()) {
			// Note that this could be nullptr if the 
			// component pool does not exist in the manager.
			return static_cast<internal::Pool<TComponent>*>(pools_[component]);
		}
		return nullptr;
	}

	/*
	* Retrieves a pointer to the system with matching system id.
	* @tparam TSystem Type of system to retrieve.
	* @param system Id of system to retrieve.
	* @return Pointer to the system, nullptr if system does not exist.
	*/
	template <typename TSystem>
	TSystem* GetSystem(const internal::Id system) {
		assert(system == GetSystemId<TSystem>());
		if (system < systems_.size()) {
			// Note that this could be nullptr if the 
			// system has not been added to the manager.
			return static_cast<TSystem*>(systems_[system]);
		}
		return nullptr;
	}


	// Full implementation of AddSystem.
	// Separated to decrease compiler errors and ease debugging.
	template <typename TSystem>
	void AddSystemImpl() {
		auto system_id = GetSystemId<TSystem>();
		// Expand systems vector if system id exceeds current size.
		if (system_id >= systems_.size()) {
			systems_.resize(static_cast<std::size_t>(system_id) + 1, nullptr);
		}
		// Destroy system if it already exists in the manager.
		delete GetSystem<TSystem>(system_id);
		auto system = new TSystem();
		assert(system != nullptr && "Could not create new system correctly");
		// Generates the system's internal component bitset.
		system->TSystem::SetComponentDependencies();
		// Sets system parent manager.
		// This avoids having to implement an identical
		// constructor separately for each system.
		system->TSystem::SetManager(this);
		systems_[system_id] = system;
	}

	// Full implementation of UpdateSystem.
	// Separated to decrease compiler errors and ease debugging.
	template <typename TSystem>
	void UpdateSystemImpl() {
		auto system_id = GetSystemId<TSystem>();
		auto system = GetSystem<TSystem>(system_id);
		// Resets system cache if it has been 
		// flagged for reset previously.
		system->TSystem::ResetCacheIfFlagged();
		// Call Update() function of system.
		system->TSystem::Update();
	}

	/*
	* Event called when a component is added to or removed from an entity.
	* Updates system cache reset flags if necessary.
	* @param entity Id of entity with changed component.
	* @param component Id of component that was changed.
	*/
	void ComponentChange(const internal::Id entity, const internal::Id component) {
		assert(entity < entities_.size());
		// Note that entities added without Refresh() 
		// are ignored by system caches.
		if (entities_[entity]) {
			for (auto& system : systems_) {
				if (system) {
					system->FlagIfDependsOn(component);
				}
			}
		}
	}

	/*
	* Retrieves a const reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component to retrieve.
	* @param entity Id of entity to retrieve component for.
	* @param component Id of component to retrieve.
	* @return Const reference to the component.
	*/
	template <typename TComponent>
	const TComponent& GetComponent(const internal::Id entity, const internal::Id component) const {
		const auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr && "Cannot retrieve component which has not been added to manager");
		const auto component_address = pool->Get(entity);
		// Debug tip: 
		// If you ended up here and want to find out which
		// entity triggered this assertion, follow the call stack.
		assert(component_address != nullptr && "Cannot get component which entity does not have");
		return *component_address;
	}

	/*
	* Retrieves a reference to the specified component type.
	* If entity does not have component, debug assertion is called.
	* @tparam TComponent Type of component to retrieve.
	* @param entity Id of entity to retrieve component for.
	* @param component Id of component to retrieve.
	* @return Reference to the component.
	*/
	template <typename TComponent>
	TComponent& GetComponent(const internal::Id entity, const internal::Id component) {
		const auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr && "Cannot retrieve component which has not been added to manager");
		auto component_address = pool->Get(entity);
		// Debug tip: 
		// If you ended up here and want to find out which
		// entity triggered this assertion, follow the call stack.
		assert(component_address != nullptr && "Cannot get component which entity does not have");
		return *component_address;
	}

	/*
	* Returns whether or not an entity has the component type.
	* @tparam TComponent Type of component to check.
	* @param entity Id of entity to check component for.
	* @param component Id of component to check.
	* @return True if entity has the component, false otherwise.
	*/
	template <typename TComponent>
	bool HasComponent(const internal::Id entity, const internal::Id component) const {
		const auto pool = GetPool<TComponent>(component);
		return pool != nullptr && pool->Has(entity);
	}

	/*
	* Returns whether or not an entity has all the given component types.
	* @tparam TComponents Types of components to check.
	* @param entity Id of entity to check components for.
	* @return True if entity has each component, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents(const internal::Id entity) const {
		return { (HasComponent<TComponents>(entity, GetComponentId<TComponents>()) && ...) };
	}

	/*
	* Adds a component to the specified entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param entity Id of entity to add component to.
	* @param component Id of component to add.
	* @param constructor_args Arguments for constructing component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(const internal::Id entity, const internal::Id component, TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>, "Cannot construct component type from given arguments");
		static_assert(std::is_destructible_v<TComponent>, "Cannot add component which does not have a valid destructor");
		// Increase pool vector size based on component id.
		if (component >= pools_.size()) {
			pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
		}
		auto pool = GetPool<TComponent>(component);
		bool new_pool = pool == nullptr;
		// If component type has not been added to manager,
		// generate a new pool for the given type.
		if (new_pool) {
			pool = new internal::Pool<TComponent>();
			pools_[component] = pool;
		}
		assert(pool != nullptr && "Could not create new component pool correctly");
		bool new_component = !pool->Has(entity);
		auto& component_reference = pool->Add(entity, std::forward<TArgs>(constructor_args)...);
		// If component or pool is new, call event to reset
		// system caches that rely on the given component type.
		if (new_pool || new_component) {
			ComponentChange(entity, component);
		}
		return component_reference;
	}

	/*
	* Removes a component from the specified entity.
	* If entity does not have the component type, nothing happens.
	* @tparam TComponent Type of component to remove.
	* @param entity Id of entity to remove component from.
	* @param component Id of component to remove.
	*/
	template <typename TComponent>
	void RemoveComponent(const internal::Id entity, const internal::Id component) {
		auto pool = GetPool<TComponent>(component);
		if (pool != nullptr) {
			// Static call to derived component pool class (no dynamic dispatch).
			bool removed = pool->internal::Pool<TComponent>::Remove(entity);
			// If component was successfully removed, inform
			// dependent systems to flag their caches for reset.
			if (removed) {
				ComponentChange(entity, component);
			}
		}
	}

	/*
	* Removes multiple components from the specified entity.
	* If entity does not a component type, it is ignored.
	* @tparam TComponents Types of components to remove.
	* @param entity Id of entity to remove components from.
	*/
	template <typename ...TComponents>
	void RemoveComponents(const internal::Id entity) {
		(RemoveComponent<TComponents>(entity, GetComponentId<TComponents>()), ...);
	}

	/*
	* Removes all components associated with an entity.
	* Results in virtual function call on each component pool.
	* @param entity Id of entity to remove all components from.
	*/
	void RemoveComponents(const internal::Id entity) {
		for (internal::Id i{ 0 }; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool != nullptr) {
				bool removed = pool->Remove(entity);
				// If component was successfully removed, inform
				// dependent systems to flag their caches for reset.
				if (removed) {
					ComponentChange(entity, i);
				}
			}
		}
	}

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

	// Systems require access to entity component access functions.
	template <typename ...T>
	friend class System;

	// Stores the next valid entity id.
	// This will be incremented if no free id is found.
	internal::Id next_entity_{ 0 };

	// Stores the current alive entity count (purely for retrieval).
	internal::Id count_{ 0 };

	// Stores whether or not a refresh is required in the manager.
	bool refresh_required_{ false };

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
	// Mutable because GetPool() is called in const and non-const
	// methods and returns a pointer to a given pool.
	mutable std::vector<internal::BasePool*> pools_;

	// Vector index corresponds to the system's unique id.
	// If a system has not been added to a manager entity, 
	// its corresponding pointer will be nullptr.
	std::vector<internal::BaseSystem*> systems_;

	// Free list of entity ids to be used 
	// before incrementing next_entity_.
	std::deque<internal::Id> free_entities_;
};

// Entity handle object.
// Allows access to manager entities through a light-weight interface.
// Contains id, version, and a pointer to the parent manager.
class Entity {
public:
	// Null entity constructor.
	Entity() = default;

	// This constructor is not intended for use by users of the ECS library.
	// It must remain public in order to allow internal vector emplace construction.
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
		assert(manager_ != nullptr && "Cannot return parent manager of a null entity");
		return *manager_;
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
		assert(IsAlive() && "Cannot retrieve component for dead or null entity");
		return manager_->GetComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Retrieves a tuple of const references to the specified component types.
	* If entity does not have components, debug assertion is called.
	* @tparam TComponent Types of components.
	* @return Tuple of const references to the requested components.
	*/
	template <typename ...TComponents>
	std::tuple<const TComponents&...> GetComponents() const {
		return std::forward_as_tuple<const TComponents&...>(GetComponent<TComponents>()...);
	}

	/*
	* Retrieves a tuple of references to the specified component types.
	* If entity does not have components, debug assertion is called.
	* @tparam TComponent Types of components.
	* @return Tuple of references to the requested components.
	*/
	template <typename ...TComponents>
	std::tuple<TComponents&...> GetComponents() {
		return std::forward_as_tuple<TComponents&...>(GetComponent<TComponents>()...);
	}

	/*
	* Adds a component to the entity.
	* If entity already has component type, it is replaced.
	* @tparam TComponent Type of component to add.
	* @tparam TArgs Types of component constructor arguments.
	* @param constructor_args Arguments for constructing the component.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(TArgs&&... constructor_args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>,
					  "Cannot construct component type from given arguments");
		static_assert(internal::type_traits::is_valid_component_v<TComponent>,
					  "Cannot add component which is not move-constructable or destructable");
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
		return IsAlive() && manager_->HasComponent<TComponent>(entity_, manager_->GetComponentId<TComponent>());
	}

	/*
	* Checks if entity has all the given components.
	* @tparam TComponents Types of components to check.
	* @return True if entity has each component, false otherwise.
	*/
	template <typename ...TComponents>
	bool HasComponents() const {
		return IsAlive() && manager_->HasComponents<TComponents...>(entity_);
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

	/*
	* Removes multiple components from the entity.
	* If entity does not have a component type, it is skipped.
	* @tparam TComponents Types of components to remove.
	*/
	template <typename ...TComponents>
	void RemoveComponents() const {
		assert(IsAlive() && "Cannot remove components from dead or null entity");
		manager_->RemoveComponents<TComponents...>(entity_);
	}

	// Removes all components from the entity.
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
	/*
	* Checks if an entity handle is valid.
	* @return True if entity is alive, false otherwise.
	*/
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(entity_, version_);
	}

	/*
	* Ids are numbers by which entities are identified in the manager.
	* @return Id of the entity.
	*/
	internal::Id GetId() const {
		return entity_;
	}

	/*
	* Versions are numbers which represent how many times an entity
	* has been reused in the manager.
	* @return Version of the entity.
	*/
	internal::Version GetVersion() const {
		return version_;
	}

	// Custom hash function requires access to id, version, and manager.
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

// Null entity object.
// Allows for comparing invalid / uninitialized 
// entities with valid manager created entities.
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

// Null entity.
// Allows for comparing invalid / uninitialized 
// entities with valid manager created entities.
inline constexpr NullEntity null{};

// Below are the definitions of functions which
// require access to methods of other classes.
// Therefore they must be placed below all classes.

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
		Resize(versions_.capacity() * 2);
	}
	assert(entity < entities_.size());
	assert(!entities_[entity] && "Cannot create new entity from live entity");
	assert(!refresh_[entity] && "Cannot create new entity from refresh marked entity");
	// Mark entity for refresh.
	refresh_[entity] = true;
	refresh_required_ = true;
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

template <typename ...TComponents>
inline std::vector<Entity> Manager::GetEntitiesWith() {
	if constexpr (sizeof...(TComponents) > 0) {
		std::vector<Entity> entities;
		entities.reserve(next_entity_);
		assert(entities_.size() == versions_.size());
		assert(next_entity_ <= entities_.size());
		// Cache component pools.
		auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
		bool manager_has_components = {
			((std::get<internal::Pool<TComponents>*>(pools) != nullptr) && ...)
		};
		if (manager_has_components) {
			// Cycle through all manager entities.
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				// If entity is alive, add its handle to the entities vector.
				if (entities_[entity]) {
					bool has_components = {
						(std::get<internal::Pool<TComponents>*>(pools)->Has(entity) && ...)
					};
					if (has_components) {
						entities.emplace_back(entity, versions_[entity], this);
					}
				}
			}
			return entities;
		}
		// If a component is foreign to the manager, 
		// there are by definition no entities with it.
		// Hence return an empty vector.
		return {};
	} else {
		return GetEntities();
	}
}

template <typename ...TComponents>
inline std::vector<Entity> Manager::GetEntitiesWithout() {
	if constexpr (sizeof...(TComponents) > 0) {
		std::vector<Entity> entities;
		entities.reserve(next_entity_);
		assert(entities_.size() == versions_.size());
		assert(next_entity_ <= entities_.size());
		// Cache component pools.
		auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
		bool manager_has_components = {
			((std::get<internal::Pool<TComponents>*>(pools) != nullptr) && ...)
		};
		// If a component in the list is foreign to the manager,
		// each entity will be definition not have it and therefore
		// return all the manager entities.
		if (!manager_has_components) {
			return GetEntities();
		} else {
			// Cycle through all manager entities.
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				// If entity is alive, add its handle to the entities vector.
				if (entities_[entity]) {
					bool does_not_have_components = {
						(!std::get<internal::Pool<TComponents>*>(pools)->Has(entity) || ...)
					};
					if (does_not_have_components) {
						entities.emplace_back(entity, versions_[entity], this);
					}
				}
			}
			return entities;
		}
	} else {
		return GetEntities();
	}
}

inline void Manager::DestroyEntities() {
	assert(entities_.size() == refresh_.size());
	assert(next_entity_ <= entities_.size());
	// Cycle through all manager entities.
	for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
		// If entity is alive, add its handle to the entities vector.
		if (entities_[entity]) {
			refresh_[entity] = true;
		}
	}
	refresh_required_ = true;
}

template <typename ...TComponents>
inline void Manager::DestroyEntitiesWith() {
	if constexpr (sizeof...(TComponents) > 0) {
		assert(entities_.size() == refresh_.size());
		assert(next_entity_ <= entities_.size());
		// Cache component pools.
		auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
		bool manager_has_components = {
			((std::get<internal::Pool<TComponents>*>(pools) != nullptr) && ...)
		};
		// If manager has not seen a given component then
		// none of the manager entities should be destroyed.
		if (manager_has_components) {
			// Cycle through all manager entities.
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				// If entity is alive, add its handle to the entities vector.
				if (entities_[entity]) {
					bool has_components = {
								(std::get<internal::Pool<TComponents>*>(pools)->Has(entity) && ...)
					};
					if (has_components) {
						refresh_[entity] = true;
					}
				}
			}
			refresh_required_ = true;
		}
	} else {
		DestroyEntities();
	}
}

template <typename ...TComponents>
inline void Manager::DestroyEntitiesWithout() {
	if constexpr (sizeof...(TComponents) > 0) {
		assert(entities_.size() == refresh_.size());
		assert(next_entity_ <= entities_.size());
		// Cache component pools.
		auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
		bool manager_has_components = {
			((std::get<internal::Pool<TComponents>*>(pools) != nullptr) && ...)
		};
		if (!manager_has_components) {
			// If all components do not exist as pools in the manager,
			// by definition each entity will not have a component.
			// Therefore mark each one for destruction.
			DestroyEntities();
		} else {
			// Cycle through all manager entities.
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				// If entity is alive, add its handle to the entities vector.
				if (entities_[entity]) {
					bool does_not_have_components = {
								(!std::get<internal::Pool<TComponents>*>(pools)->Has(entity) || ...)
					};
					if (does_not_have_components) {
						refresh_[entity] = true;
					}
				}
			}
			refresh_required_ = true;
		}
	} else {
		DestroyEntities();
	}
}

template <typename ...TComponents>
inline std::vector<std::tuple<Entity, TComponents&...>> Manager::GetEntityComponents() {
	static_assert(sizeof...(TComponents) > 0,
				  "Cannot get entity components without at least one specified component type");
	std::vector<std::tuple<Entity, TComponents&...>> vector_of_tuples;
	if (count_ > 0) {
		auto pools = std::make_tuple(GetPool<TComponents>(GetComponentId<TComponents>())...);
		bool manager_has_components = {
			((std::get<internal::Pool<TComponents>*>(pools) != nullptr) && ...)
		};
		// Cycle through all manager entities.
		if (manager_has_components) {
			vector_of_tuples.reserve(next_entity_);
			assert(entities_.size() == versions_.size());
			assert(next_entity_ <= entities_.size());
			// Cycle through all manager entities.
			for (internal::Id entity{ 0 }; entity < next_entity_; ++entity) {
				if (entities_[entity]) {
					bool has_components = {
						(std::get<internal::Pool<TComponents>*>(pools)->Has(entity) && ...)
					};
					if (has_components) {
						vector_of_tuples.emplace_back(Entity{ entity, versions_[entity], this }, (*std::get<internal::Pool<TComponents>*>(pools)->Get(entity))...);
					}
				}
			}
		}
	}
	return vector_of_tuples;
}

template <typename ...TRequiredComponents>
inline void System<TRequiredComponents...>::SetComponentDependencies() {
	std::array<internal::Id, sizeof...(TRequiredComponents)> components{ Manager::GetComponentId<TRequiredComponents>()... };
	if (components.size() > 0) {
		// Find the largest component id in the parameter pack.
		// Useful so the component bitset can be reserved to the 
		// correct capacity.
		auto size{ *std::max_element(components.begin(), components.end()) + 1 };
		components_.reserve(size);
		components_.resize(size, false);
		for (auto component : components) {
			assert(component < components_.size());
			components_[component] = true;
		}
	}
}

template <typename ...TRequiredComponents>
inline void System<TRequiredComponents...>::ResetCacheIfFlagged() {
	if (reset_required_) {
		// Fetch new entities vector of tuples from the manager.
		entities = manager_->GetEntityComponents<TRequiredComponents...>();
		reset_required_ = false;
	}
}

} // namespace ecs

namespace std {

// Custom hashing function for ecs::Entity class.
// This allows for use of unordered maps and sets with entities as keys.
template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& k) const {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<ecs::Manager*>()(k.manager_);
		h = h * 31 + std::hash<ecs::internal::Id>()(k.entity_);
		h = h * 31 + std::hash<ecs::internal::Version>()(k.version_);
		return h;
	}
};

} // namespace std