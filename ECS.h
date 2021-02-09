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
#include <utility> // std::move, std::exchange, etc
#include <algorithm> // std::iter_swap, etc

// TODO: Check that GetDeadEntityCount and GetEntityCount functions work as intended.
// TODO: Check that RemoveComponents functions work as intended.
// TODO: Update documentation with GetDeadEntityCount, GetEntityCount, RemoveComponents.
// TODO: Check that documentation is fully up to date.
// TODO: Write tests for is_constructible.
// TODO: Write tests for ForEach.
// TODO: Write tests for DestroyEntities.
// TODO: Add const& version of GetComponents and express non-const version in terms of this.
// TODO: Add test that last elements of sparse_set_ is never INVALID_INDEX.

// BIG TODO: Consider switching to mandatory refresh with deleted entities existing until end of update loop.

namespace ecs {

using EntityId = std::uint32_t;
using EntityVersion = std::uint32_t;
using ComponentId = std::uint32_t;
using AtomicComponentId = std::atomic_uint32_t;
using SystemId = std::uint32_t;
using AtomicSystemId = std::atomic_uint32_t;
using ManagerId = std::int32_t;
using Offset = std::int64_t;

// Constants

// Null entity for comparison operations
constexpr EntityId null = 0;

namespace internal {

// First valid entity for internal for-loops.
constexpr EntityId first_valid_entity_id = null + 1;

// Invalid version.
constexpr EntityVersion null_version = 0;
// Invalid manager id.
constexpr ManagerId null_manager_id = -1;

using ComponentIndex = std::int64_t;

constexpr ComponentIndex INVALID_INDEX = -1;

// Destructor pointer alias.
using ComponentFunction = void (*)(void*);

// Address, destructor, copy, move.
using GeneralComponent = std::tuple<void*, ComponentFunction, ComponentFunction, ComponentFunction>;

class BasePool {
public:
	virtual void Destroy() = 0;
	virtual std::unique_ptr<BasePool> Clone() = 0;
private:
};

// Object which allows for contiguous storage of components of a single type (with runtime addition!).
template <typename Component>
class Pool : public BasePool {
public:

	Pool() = default;
	Pool(const std::vector<ComponentIndex>& sparse_set, const std::vector<Component>& dense_set) : sparse_set_{ sparse_set }, dense_set_{ dense_set } {}
	~Pool() = default;
	Pool(const Pool&) = default;
	Pool& operator=(const Pool&) = default;
	Pool(Pool&&) = default;
	Pool& operator=(Pool&&) = default;

	virtual void Destroy() override final {
		dense_set_.clear();
		sparse_set_.clear();
	}

	virtual std::unique_ptr<BasePool> Clone() override final {
		return std::make_unique<Pool<Component>>(sparse_set_, dense_set_);
	}

	template <typename ...TArgs>
	Component& Add(const EntityId id, TArgs&&... args) {
		if (id < sparse_set_.size()) {
			auto dense_index = sparse_set_[id];
			if (static_cast<std::size_t>(dense_index) < dense_set_.size()) {
				auto& component = dense_set_[dense_index];
				component = Component(std::forward<TArgs>(args)...);
				return component;
			}
		} else {
			sparse_set_.resize(id + 1, INVALID_INDEX);
		}
		sparse_set_[id] = dense_set_.size();
		return dense_set_.emplace_back(std::forward<TArgs>(args)...);
	}

	bool Remove(const EntityId id) {
		if (id < sparse_set_.size()) {
			auto& dense_index = sparse_set_[id];
			if (static_cast<std::size_t>(dense_index) < dense_set_.size() && dense_index != INVALID_INDEX) {
				if (dense_set_.size() > 1) {
					if (id == sparse_set_.size() - 1) {
						dense_index = INVALID_INDEX;
						auto result = std::find_if(sparse_set_.rbegin(), sparse_set_.rend(),[](ComponentIndex index) { return index != INVALID_INDEX; });
						sparse_set_.erase(result.base(), sparse_set_.end());
					} else {
						std::iter_swap(dense_set_.begin() + dense_index, dense_set_.end() - 1);
						sparse_set_.back() = dense_index;//INVALID_INDEX;
						dense_index = INVALID_INDEX;
					}
					dense_set_.pop_back();
				} else {
					sparse_set_.clear();
					dense_set_.clear();
				}

				// TODO: Add test that last elements of sparse_set_ is never INVALID_INDEX.

				//assert({ bool test = (sparse_set_.size() > 0) ? (*sparse_set_).back() != INVALID_INDEX : true; test });
				return true;
			}
		}
		return false;
	}

	const Component& Get(const EntityId id) const {
		assert(id < sparse_set_.size());
		auto dense_index = sparse_set_[id];
		assert(dense_index != INVALID_INDEX);
		assert(static_cast<std::size_t>(dense_index) < dense_set_.size());
		return dense_set_[dense_index];
	}

	Component& Get(const EntityId id) {
		return const_cast<Component&>(static_cast<const Pool&>(*this).Get(id));
	}

	bool Has(const EntityId id) const {
		return id < sparse_set_.size() && sparse_set_[id] != INVALID_INDEX;
	}
private:
	std::vector<ComponentIndex> sparse_set_;
	std::vector<Component> dense_set_;
};

// Holds the state of an entity id
struct EntityData {
	EntityData() = default;
	~EntityData() = default;
	EntityData(const EntityData&) = default;
	EntityData& operator=(const EntityData&) = default;
	EntityData(EntityData&&) = default;
	EntityData& operator=(EntityData&&) = default;
	// Internal 'version' counter which invalidates all previous handles to an entity when it is destroyed
	friend inline bool operator==(const EntityData& lhs, const EntityData& rhs) {
		return lhs.version == rhs.version && lhs.alive == rhs.alive;
	}
	EntityVersion version = null_version;
	// Determines if the entity id is currently in use (or invalidated / free)
	bool alive = false;
};

} // namespace internal

class Entity;
class Manager;

class BaseSystem { // System template class interface for homogenous storage of system pointers
public:
	virtual void Init(Manager* manager) = 0;
	virtual void Update() = 0;
	virtual Manager& GetManager() = 0;
	virtual bool DependsOn(ComponentId component_id) const = 0;
	virtual void ResetCache() = 0;
	virtual bool GetCacheRefreshRequired() const = 0;
	virtual void SetCacheRefreshRequired(bool required) = 0;
	virtual void SetComponentDependencies() = 0;
	virtual ~BaseSystem() {}
};

// Component and entity registry for the ECS.
class Manager {
public:
	// Important: Initialize an invalid 0th index pool index in entities_ (null entity's index).
	Manager() : id_{ ++ManagerCount() } {}
	// Invalidate manager id, reset entity count, and call destructors on everything.
	~Manager() = default;
	// Clear the manager, this will destroy all entities and components in the memory of the manager.
	void Clear() {
		entity_count_ = 0;
		// Keep the null entity.
		entities_.resize(1);
		entities_.shrink_to_fit();
		pools_.clear();
		pools_.shrink_to_fit();
		free_entity_ids_.clear();
		free_entity_ids_.shrink_to_fit();
		// Let systems know their caches are invalid.
		for (auto& system : systems_) {
			if (system != nullptr) {
				system->SetCacheRefreshRequired(true);
			}
		}
	}
	// Manager copying disabled. Use Clone() if you wish to generate a manager with the same entity/component composition.

	/**
	 * @brief Manager copying disabled. Use Clone() instead if you wish to generate a manager with the same entity and component composition. 
	 */
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&& move) noexcept : entity_count_{ move.entity_count_ }, id_{ move.id_ }, entities_{ std::exchange(move.entities_, {}) }, pools_{ std::exchange(move.pools_, {}) }, free_entity_ids_{ std::exchange(move.free_entity_ids_, {}) }, systems_{ std::exchange(move.systems_, {}) } {
		move.entity_count_ = 0;
		move.id_ = internal::null_manager_id;
	}
	Manager& operator=(Manager&& move) noexcept {
		entities_.clear();
		pools_.clear();
		free_entity_ids_.clear();
		systems_.clear();

		entity_count_ = move.entity_count_;
		id_ = move.id_;
		entities_ = std::exchange(move.entities_, {});
		pools_ = std::exchange(move.pools_, {});
		free_entity_ids_ = std::exchange(move.free_entity_ids_, {});
		systems_ = std::exchange(move.systems_, {});

		move.entity_count_ = 0;
		move.id_ = internal::null_manager_id;
	}

	Manager Clone() const {
		ecs::Manager clone; 
		// id_ already set to next available one inside default constructor.
		clone.entity_count_ = entity_count_;
		clone.entities_ = entities_;
		clone.pools_.reserve(pools_.size());
		for (auto& pool : pools_) {
			clone.pools_.emplace_back(std::move(pool->Clone()));
		}
		return clone;
	}

	/**
	* @note Composition equivalence is not enough for equality.
	*
	* @return True if manager ids match, false otherwise.
	*/
	friend inline bool operator==(const Manager& lhs, const Manager& rhs) {
		return lhs.id_ == rhs.id_;
	}
	/**
	* @note Composition difference is not enough for inequality.
	*
	* @return True if manager ids do not match, false otherwise.
	*/
	friend inline bool operator!=(const Manager& lhs, const Manager& rhs) {
		return !operator==(lhs, rhs);
	}

	/**
	* @return True if the other manager is composed of identical entities, false otherwise.
	*/
	bool Equivalent(const Manager& other) {
		return entity_count_ == other.entity_count_ && entities_ == other.entities_ && pools_ == other.pools_;
	}

	friend class Entity;
	template <typename ...Ts>
	friend class System;
	// Add a new entity to the manager and return a handle to it.
	Entity CreateEntity();
	// Add a system to the manager, it is exists it will be replaced.
	// Arguments are optional and are passed into system constructor.
	template <typename T, typename ...TArgs>
	void AddSystem(TArgs... args);
	// Remove a system from the manager.
	template <typename T>
	void RemoveSystem();
	// Returns whether or not the manager has a specific system.
	template <typename T>
	bool HasSystem();
	// Update a specific system in the manager.
	template <typename T>
	void Update();
	// Cycle through every single entity in the manager.
	// Populates the lambda function's parameter list with a handle to the current entity.
	//template <typename T>
	//void ForEachEntity(T&& function);
	//// Cycle through each entity in the manager which has all of the given components.
	//// Populates the lambda function's parameter list with a handle to the current entity and a reference to each component (in order of template argument types).
	//template <typename ...Ts, typename T>
	//void ForEach(T&& function);
	// Return the number of entities which are currently alive in the manager.
	std::size_t GetEntityCount() const;
	// Return the number of entities which are currently not alive in the manager.
	std::size_t GetDeadEntityCount() const;
	// Retrieve a vector of handles to each entity in the manager.
	std::vector<Entity> GetEntities();
	// Retrieve a vector of handles to each entity in the manager which has all of the given components.
	template <typename ...Ts>
	std::vector<Entity> GetEntitiesWith();
	// Retrieve a vector of handles to each entity in the manager which does not have all of the given components.
	template <typename ...Ts>
	std::vector<Entity> GetEntitiesWithout();
	// Destroy all entities in the manager.
	// Note: this will not clear the memory used by the components of destroyed entities.
	// Use Clear() if you wish to free up the memory associated with those components.
	void DestroyEntities();
	// Destroy entities which have all of the given components.
	template <typename ...Ts>
	void DestroyEntitiesWith() {
		auto entities = GetEntitiesWith<Ts...>();
		for (auto entity : entities) {
			entity.Destroy();
		}
	}
	// Destroy entities which do not have all of the given components.
	template <typename ...Ts>
	void DestroyEntitiesWithout() {
		auto entities = GetEntitiesWithout<Ts...>();
		for (auto entity : entities) {
			entity.Destroy();
		}
	}
	// Returns a vector of tuples where the first element is an entity and the rest are the requested components, only retrieves entities which have each component
	template <typename ...Ts>
	std::vector<std::tuple<Entity, Ts&...>> GetComponentTuple();
	// Return the manager's unique id. Can be useful for comparison purposes.
	ManagerId GetId() const { return id_; }
private:
	// Internally used method implementations.

	// Event which is called upon component addition or removal from an entity.
	void ComponentChange(const EntityId id, const ComponentId component_id, bool loop_entity);
	// Add entity id to deletion list but do not invalidate any handles.
	void DestroyEntity(const EntityId id, const EntityVersion version, bool loop_entity) {
		if (IsAlive(id, version)) {
			/*RemoveComponents(id, loop_entity);*/

			// TODO: Remove components.

			// Increment entity version, this will invalidate all entity handles with the previous version
			assert(id < entities_.size() && "Could not increment dead entity id");
			++entities_[id].version;
			entities_[id].alive = false;
			free_entity_ids_.emplace_back(id);
		}
	}
	//// Call a lambda function and populate its parameter list with a reference to each requested component.
	//template <typename ...Ts, typename T, std::size_t... component_id>
	//void ForEachHelper(T&& function, const EntityId id, const std::array<ComponentId, sizeof...(Ts)>& component_ids, std::index_sequence<component_id...>);
	//// Wrapper for creating an index sequence in order to access each element of the array of component ids.
	//template <typename ...Ts, typename T>
	//void ForEachInvoke(T&& function, const EntityId id, const std::array<ComponentId, sizeof...(Ts)>& component_ids) {
	//	ForEachHelper<Ts...>(std::forward<T>(function), id, component_ids, std::make_index_sequence<sizeof...(Ts)>{});
	//}

	// GetComponents without passing a component id.
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents(const EntityId id) {
		return std::forward_as_tuple<Ts&...>(GetComponent<Ts>(id, GetComponentId<Ts>())...);
	}
	// Check if a system id exists in the manager.
	bool IsValidSystem(const SystemId id) const {
		return id < systems_.size() && systems_[id];
	}
	// Check if an entity id exists in the manager.
	bool IsValidEntity(const EntityId id) const {
		return id != null && id < entities_.size() && entities_[id].version != internal::null_version;
	}
	// Check if a given entity id is considered alive.
	bool IsAlive(const EntityId id, const EntityVersion version) const {
		return IsValidEntity(id) && entities_[id].alive && entities_[id].version == version;
	}
	// GetComponent implementation.
	template <typename Component>
	const Component& GetComponent(const EntityId id, const ComponentId component_id) const {
		assert(IsValidEntity(id) && "Cannot get component from an invalid entity");
		assert(component_id < pools_.size());
		auto pool = static_cast<internal::Pool<Component>*>(pools_[component_id].get());
		assert(pool != nullptr);
		return pool->Get(id);
	}
	template <typename Component>
	Component& GetComponent(const EntityId id, const ComponentId component_id) {
		return const_cast<Component&>(static_cast<const Manager&>(*this).GetComponent<Component>(id, component_id));
	}
	// RemoveComponents for all components implementation.
	//void RemoveComponents(const EntityId id, bool loop_entity) {
	//	auto pool_count = static_cast<ComponentId>(pools_.size());
	//	for (ComponentId component_id = 0; i < pool_count; ++component_id) {
	//		pools_[component_id]->Remove();
	//		if (pool.IsValid()) {
	//			pool.RemoveComponentAddress(id);
	//			// Pool index i in pools_ vector is the corresponding component's id.
	//			ComponentChange(id, i, loop_entity);
	//		}
	//	}
	//}
	//// RemoveComponents implementation.
	//template <typename ...Ts>
	//void RemoveComponents(const EntityId id, bool loop_entity) {
	//	(RemoveComponent<Ts>(id, loop_entity), ...);
	//}
	// HasComponent implementation.

	template <typename Component>
	bool HasComponent(const EntityId id) const {
		assert(IsValidEntity(id) && "Cannot check if invalid entity has component");
		auto component_id = GetComponentId<Component>();
		assert(component_id < pools_.size());
		auto pool = static_cast<internal::Pool<Component>*>(pools_[component_id].get());
		return pool != nullptr && pool->Has(id);
	}
	template <typename ...Components>
	bool HasComponents(const EntityId id) const {
		return { (HasComponent<Components>(id) && ...) };
	}
	// AddComponent implementation.
	template <typename Component, typename ...TArgs>
	Component& AddComponent(const EntityId id, bool loop_entity, TArgs&&... args) {
		static_assert(std::is_constructible_v<Component, TArgs...>, "Cannot add component with given constructor argument list");
		static_assert(std::is_destructible_v<Component>, "Cannot add component without valid destructor");
		assert(IsValidEntity(id) && "Cannot add component to invalid entity");
		auto component_id = GetComponentId<Component>();
		if (component_id >= pools_.size()) {
			pools_.resize(component_id + 1);
		}
		auto pool = static_cast<internal::Pool<Component>*>(pools_[component_id].get());
		bool new_component = pool == nullptr;
		if (new_component) {
			auto new_pool = std::make_unique<internal::Pool<Component>>();
			pool = new_pool.get();
			pools_[component_id] = std::move(new_pool);
		}
		assert(pool != nullptr);
		auto& component = pool->Add(id, std::forward<TArgs>(args)...);

		if (new_component) {
			// Small design decision: Don't trigger ComponentChange event when a component is replaced.
			// As this does not invalidate any references, system caches don't have to be refreshed.
			ComponentChange(id, component_id, loop_entity);
		}
		return component;
	}
	// RemoveComponent implementation.
	template <typename Component>
	void RemoveComponent(const EntityId id, bool loop_entity) {
		assert(IsValidEntity(id) && "Cannot remove component from invalid entity");
		auto component_id = GetComponentId<Component>();
		assert(component_id < pools_.size());
		auto pool = static_cast<internal::Pool<Component>*>(pools_[component_id].get());
		if (pool != nullptr) {
			bool removed = pool->Remove(id);
			if (removed) {
				ComponentChange(id, component_id, loop_entity);
			}
		}
	}
	// Double the entity id vector if capacity is reached.
	void GrowEntitiesIfNeeded(const EntityId id) {
		if (id >= entities_.size()) {
			auto capacity = entities_.capacity() * 2;
			assert(capacity != 0 && "Capacity is 0, cannot double size of entities_ vector");
			entities_.resize(capacity);
		}
	}
	// Create / retrieve a unique id for each component class.
	template <typename T>
	static ComponentId GetComponentId() {
		static ComponentId id = ComponentCount()++;
		return id;
	}
	// Create / retrieve a unique id for each system class.
	template <typename T>
	static SystemId GetSystemId() {
		static SystemId id = SystemCount()++;
		return id;
	}
	// Total entity count (dead / invalid and alive).
	EntityId entity_count_{ 0 };
	// Unique manager identifier (used to compare managers).
	ManagerId id_{ internal::null_manager_id };
	// Dense vector of entity ids that map to specific metadata (version and dead/alive).
	std::vector<internal::EntityData> entities_{ {} };
	// Sparse vector of component pools.
	mutable std::vector<std::unique_ptr<internal::BasePool>> pools_;
	// Free list of entity ids to be used before incrementing count.
	std::deque<EntityId> free_entity_ids_;
	// Sparse vector of system pointers.
	std::vector<std::unique_ptr<BaseSystem>> systems_;
	// Used for generating unique manager ids.
	static ManagerId& ManagerCount() { static ManagerId id{ internal::null_manager_id }; return id; }
	// Important design decision: Component ids shared among all created managers, i.e. struct Position has id '3' in all manager instances, as opposed to the order in which a component is first added to each manager.
	static ComponentId& ComponentCount() { static ComponentId id{ 0 }; return id; }
	// Similarly to component ids, system ids are shared for all managers.
	static SystemId& SystemCount() { static SystemId id{ 0 }; return id; }

};

// Template class for ECS systems, template arguments are components which the system requires of its entities.
// Each system has access to a protected 'entities' variable.
// Example:
// struct MySystem : public System<Transform, RigidBody>;
// 'entities' can be used inside the system's methods like so:
// auto [auto first_entity, transform, rigid_body] = entities[0];
// or using a loop:
// for (auto [entity, transform, rigid_body] : entities) {
//	... use handle / component references here ... }
// If a system-required component is removed from an entity, that entity will be removed from 'entities'.
template <typename ...Cs>
class System : public BaseSystem {
public:
	friend class Manager;
protected:
	// A vector of tuples where the first tuple element is a copy of an entity handle and the rest are references to that entity's components as determined by the system's required components.
	// Entities are cached in the system which allows for fast traversal.
	std::vector<std::tuple<Entity, Cs&...>> entities;
	// Retrieve the manager, for possible use in system methods
	virtual Manager& GetManager() override final {
		assert(manager_ != nullptr && "Cannot get manager as system has not been properly initialized");
		return *manager_;
	}
	// Make sure to also call this destructor when potentially overriding it.
	virtual ~System() override {
		entities.~vector();
		component_bitset_.~vector();
		manager_ = nullptr;
	}
private:
	// Add manager and call initial cache reset.
	virtual void Init(Manager* manager) override final {
		manager_ = manager;
		assert(manager_ != nullptr && "Could not initialize system with invalid manager");
		ResetCache();
	}
	// Refetch tuples of entity handles and components which match the system's required components. Called in the system constructor and when one of the system's required components is added or removed from an entity in the manager.
	virtual void ResetCache() override final;
	// Return whether or not a cache refresh is required after updating the system
	virtual bool GetCacheRefreshRequired() const override final {
		return cache_refresh_required_;
	}
	// Tells the system that once the update function is called, it must refresh its 'entities' variable (cache update).
	virtual void SetCacheRefreshRequired(bool required) override final {
		cache_refresh_required_ = required;
	}
	// Check if a component id is required by the system
	virtual bool DependsOn(ComponentId component_id) const override final {
		return component_id < component_bitset_.size() && component_bitset_[component_id];
	}
	void SetComponentDependencies() override final {
		(AddComponentDependency<Cs>(), ...);
	}
	// Add a template argument into a dynamic component bitset so system dependency on a component can be checked.
	template <typename C>
	void AddComponentDependency() {
		ComponentId component_id = Manager::GetComponentId<C>();
		if (component_id >= component_bitset_.size()) {
			component_bitset_.resize(static_cast<std::size_t>(component_id) + 1, false);
		}
		component_bitset_[component_id] = true;
	}
	// A dynamic bitset of components which the system requires
	std::vector<bool> component_bitset_;
	Manager* manager_ = nullptr;
	bool cache_refresh_required_ = false;
};

// ECS Entity Handle
class Entity {
public:
	// Default construction to null entity.
	Entity(EntityId id = null) : id_{ id }, version_{ internal::null_version }, manager_{ nullptr }, loop_entity_{ false } {}
	// Entity construction within the manager.
	Entity(EntityId id, EntityVersion version, Manager* manager, bool loop_entity = false) : id_{ id }, version_{ version }, manager_{ manager }, loop_entity_{ loop_entity } {}
	~Entity() = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	friend class Manager;
	// Returns the entity's unique id
	EntityId GetId() const {
		return id_;
	}
	// Returns the entity's version (incremented upon death)
	EntityVersion GetVersion() const {
		return version_;
	}
	// Returns a pointer to the entity's manager
	Manager* GetManager() const {
		return manager_;
	}
	// Returns whether or not the entity is alive
	bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(id_, version_);
	}
	// Returns whether or not the entity is valid (not null)
	bool IsValid() const {
		return manager_ != nullptr && manager_->IsValidEntity(id_);
	}
	// Add a component to the entity, returns a reference to the new component.
	template <typename Component, typename ...Args>
	Component& AddComponent(Args&&... constructor_args) {
		static_assert(std::is_constructible_v<Component, Args...>, "Cannot add component with given constructor argument list");
		static_assert(std::is_destructible_v<Component>, "Cannot add component without valid destructor");
		assert(IsValid() && "Cannot add component to null entity");
		assert(IsAlive() && "Cannot add component to dead entity");
		return manager_->AddComponent<Component>(id_, loop_entity_, std::forward<Args>(constructor_args)...);
	}
	// Remove a component from the entity.
	template <typename T>
	void RemoveComponent() {
		assert(IsValid() && "Cannot remove component from null entity");
		assert(IsAlive() && "Cannot remove component from dead entity");
		return manager_->RemoveComponent<T>(id_, loop_entity_);
	}
	//// Remove all components from the entity.
	//void RemoveComponents() {
	//	assert(IsValid() && "Cannot remove all components from null entity");
	//	assert(IsAlive() && "Cannot remove all components from dead entity");
	//	return manager_->RemoveComponents(id_, loop_entity_);
	//}
	//// Remove multiple components from the entity.
	//template <typename ...Ts>
	//void RemoveComponents() {
	//	assert(IsValid() && "Cannot remove components from null entity");
	//	assert(IsAlive() && "Cannot remove components from dead entity");
	//	return manager_->RemoveComponents<Ts...>(id_, loop_entity_);
	//}
	// Check if the entity has a component.
	template <typename T>
	bool HasComponent() const {
		assert(IsValid() && "Cannot check if null entity has a component");
		//assert(IsAlive() && "Cannot check if dead entity has a component");
		return manager_->HasComponent<T>(id_);
	}
	// Check if the entity has all the given components.
	template <typename ...Ts>
	bool HasComponents() const {
		assert(IsValid() && "Cannot check if null entity has components");
		//assert(IsAlive() && "Cannot check if dead entity has components");
		return manager_->HasComponents<Ts...>(id_);
	}
	// Returns a const reference to a component.
	// Will throw if retrieving a nonexistent component (surround with HasComponent if uncertain).
	template <typename T>
	const T& GetComponent() const {
		assert(IsValid() && "Cannot get component from null entity");
		assert(IsAlive() && "Cannot get component from dead entity");
		return manager_->GetComponent<T>(id_, manager_->GetComponentId<T>());
	}
	// Returns a reference to a component.
	// Will throw if retrieving a nonexistent component (surround with HasComponent if uncertain).
	template <typename T>
	T& GetComponent() {
		return const_cast<T&>(static_cast<const Entity&>(*this).GetComponent<T>());
	}
	// Returns a tuple of references to components.
	// Will throw if retrieving a nonexistent component (surround with HasComponents if uncertain).
	template <typename ...Ts>
	std::tuple<Ts&...> GetComponents() {
		assert(IsValid() && "Cannot get components from null entity");
		assert(IsAlive() && "Cannot get components from dead entity");
		return manager_->GetComponents<Ts...>(id_);
	}
	// Destroy / Kill / Murder an entity and refresh the appropriate manager (this will invalidate the entity handle).
	void Destroy() {
		if (IsAlive()) {
			manager_->DestroyEntity(id_, version_, loop_entity_);
		}
	}

	// Entity comparison operators, the duplication allows avoiding implicit conversion of ecs::null into an entity

	inline bool operator==(const Entity& other) const {
		return manager_ == other.manager_ && id_ == other.id_ && version_ == other.version_;
	}
	friend inline bool operator==(const Entity& lhs, const EntityId& rhs) {
		return lhs.id_ == rhs;
	}
	friend inline bool operator==(const EntityId& lhs, const Entity& rhs) {
		return lhs == rhs.id_;
	}
	friend inline bool operator!=(const Entity& lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
	friend inline bool operator!=(const EntityId& lhs, const Entity& rhs) {
		return !(lhs == rhs);
	}
	friend inline bool operator!=(const Entity& lhs, const EntityId& rhs) {
		return !(lhs == rhs);
	}
	// Bool operator returns true if entity is alive and valid.
	operator bool() const {
		return IsAlive() && IsValid();
	}

private:
	EntityId id_;
	EntityVersion version_;
	Manager* manager_;
	bool loop_entity_;
};

struct EntityComparator {
	bool operator()(const Entity& a, const Entity& b) const {
		return a.GetId() < b.GetId();
	}
};

inline Entity Manager::CreateEntity() {
	EntityId id = null;
	if (free_entity_ids_.size() > 0) { // Pick id from free list before trying to increment entity counter.
		id = free_entity_ids_.front();
		free_entity_ids_.pop_front();
	} else {
		id = ++entity_count_;
	}
	assert(id != null && "Could not create entity due to lack of free entity ids");
	GrowEntitiesIfNeeded(id);
	assert(id < entities_.size() && "Entity id outside of range of entities vector");
	entities_[id].alive = true;
	return Entity{ id, ++entities_[id].version, this };
}
inline std::size_t Manager::GetEntityCount() const {
	return entity_count_ - GetDeadEntityCount();
	/*
	// TODO: Remove after checking the above works.
	std::size_t entities = 0;
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (entity_data.alive) {
			entities += 1;
		}
	}
	return entities;
	*/
}
inline std::size_t Manager::GetDeadEntityCount() const {
	return free_entity_ids_.size();
}
inline std::vector<Entity> Manager::GetEntities() {
	return GetEntitiesWith<>();
}
template <typename ...Components>
inline std::vector<Entity> Manager::GetEntitiesWith() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	// Cycle through all manager entities.
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (HasComponents<Components...>(id) && entity_data.alive) {
			// If entity's components match the required ones and it is alive, add its handle to the vector.
			entities.emplace_back(id, entity_data.version, this);
		}
	}
	entities.shrink_to_fit();
	return entities;
}
template <typename ...Components>
inline std::vector<Entity> Manager::GetEntitiesWithout() {
	std::vector<Entity> entities;
	entities.reserve(entity_count_);
	// Cycle through all manager entities.
	for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
		auto& entity_data = entities_[id];
		if (!HasComponents<Components...>(id) && entity_data.alive) {
			// If entity's components DO NOT MATCH the required ones and it is alive, add its handle to the vector.
			entities.emplace_back(id, entity_data.version, this);
		}
	}
	entities.shrink_to_fit();
	return entities;
}

template <class... Formats, size_t N, class = std::enable_if_t<(N == sizeof...(Formats))>>
inline std::tuple<Formats...> as_tuple(std::array<char*, N> const& arr) {
	return as_tuple<Formats...>(arr, std::make_index_sequence<N>{});
}

template <typename ...Components>
inline std::vector<std::tuple<Entity, Components&...>> Manager::GetComponentTuple() {
	std::vector<std::tuple<Entity, Components&...>> vector_of_tuples;
	if (entity_count_ > 0) {
		auto pools = std::make_tuple(static_cast<internal::Pool<Components>*>(pools_[GetComponentId<Components>()].get())...);
		bool manager_has_components = { (std::get<internal::Pool<Components>*>(pools) && ...) };
		// Cycle through all manager entities.
		if (manager_has_components) {
			vector_of_tuples.reserve(entity_count_);
			assert(static_cast<std::size_t>(internal::first_valid_entity_id) < entities_.size() && "Entity id out of range");
			assert(static_cast<std::size_t>(entity_count_) < entities_.size() && "Entity id out of range");
			for (EntityId id = internal::first_valid_entity_id; id <= entity_count_; ++id) {
				auto& entity_data = entities_[id];
				if (entity_data.alive) {
					bool entity_has_components = { (std::get<internal::Pool<Components>*>(pools)->Has(id) && ...) };
					if (entity_has_components) {
						vector_of_tuples.emplace_back(Entity{ id, entity_data.version, this, true }, std::get<internal::Pool<Components>*>(pools)->Get(id)...);
					}
				}
			}
			vector_of_tuples.shrink_to_fit();
		}
	}
	return vector_of_tuples;
}
//template <typename ...Ts, typename T, std::size_t... component_id>
//inline void Manager::ForEachHelper(T&& function, const EntityId id, const std::array<ComponentId, sizeof...(Ts)>& component_ids, std::index_sequence<component_id...>) {
//	function(Entity{ id, entities_[id].version, this, true }, GetComponent<Ts>(id, component_ids[component_id])...);
//}
//template <typename ...Ts, typename T>
//inline void Manager::ForEach(T&& function) {
//	// TODO: Write some tests for lambda function parameters.
//	std::vector<std::tuple<Entity, Ts&...>> entities = GetComponentTuple<Ts...>();
//	for (std::tuple<Entity, Ts&...> tuple : entities) {
//		// Call the lambda for each entity with its tuple
//		std::apply(function, tuple);
//	}
//}
//template <typename T>
//inline void Manager::ForEachEntity(T&& function) {
//	// TODO: Write some tests for lambda function parameters.
//	auto entities = GetEntities();
//	for (auto entity : entities) {
//		// Call the lambda for each entity in the manager
//		function(entity);
//	}
//}
inline void Manager::DestroyEntities() {
	auto entities = GetEntities();
	for (auto& entity : entities) {
		entity.Destroy();
	}
}
inline void Manager::ComponentChange(const EntityId id, const ComponentId component_id, bool loop_entity) {
	for (auto& system : systems_) {
		if (system) {
			if (system->DependsOn(component_id)) {
				assert(IsValidEntity(id) && "Cannot trigger component change event for invalid entity");
				// Apparently ignore this comment... // If an entity is a loop entity (from system 'entites') do not invalidate the cache as the range-based for-loop iterators will be invalidated and this will lead to undefined behavior.
				system->SetCacheRefreshRequired(true);
			}
		}
	}
}
template <typename T, typename ...TArgs>
inline void Manager::AddSystem(TArgs... args) {
	// TODO: Add tests to check args match system constructor args.
	static_assert(std::is_base_of_v<BaseSystem, T>, "Cannot add a system to the manager which does not derive from ecs::System");
	static_assert(std::is_constructible_v<T, TArgs...>, "Cannot construct system with given constructor argument list");
	SystemId system_id = GetSystemId<T>();
	if (system_id >= systems_.size()) {
		systems_.resize(static_cast<std::size_t>(system_id) + 1);
	}
	RemoveSystem<T>();
	auto& system = systems_[system_id];
	// If you're getting an error relating to no '=' conversion being deleted or no conversion possible,
	// check that your system inherits from ecs::System AND make sure you have the 'public' keyword in front of it.
	system = std::make_unique<T>(std::forward<TArgs>(args)...);
	system->SetComponentDependencies();
	system->Init(this);
}
template <typename T>
inline bool Manager::HasSystem() {
	return std::is_base_of_v<BaseSystem, T> && IsValidSystem(GetSystemId<T>());
}
template <typename T>
inline void Manager::RemoveSystem() {
	if (HasSystem<T>()) {
		SystemId system_id = GetSystemId<T>();
		auto& system = systems_[system_id];
		// Call system destructor and set the system's unique pointer to nullptr.
		system.reset();
		system = nullptr;
	}
}
template <typename T>
inline void Manager::Update() {
	static_assert(std::is_base_of_v<BaseSystem, T>, "Cannot update a system which does not derive from ecs::System");
	SystemId system_id = GetSystemId<T>();
	assert(IsValidSystem(system_id) && "Cannot update a system which does not exist in manager");
	auto& system = systems_[system_id];
	assert(system && system.get() != nullptr && "Invalid system pointer, check system creation");
	if (system->GetCacheRefreshRequired()) {
		system->ResetCache();
	}
	system->Update();
	if (system->GetCacheRefreshRequired()) {
		system->ResetCache();
	}
}
template <typename ...Cs>
inline void System<Cs...>::ResetCache() {
	entities = manager_->GetComponentTuple<Cs...>();
	SetCacheRefreshRequired(false);
	/*
	// Checking whether entity exists isn't needed as this function is only entered upon addition / removal of a NEW component, therefore entity could not have existed before / not existed before.
	auto entity_exists = std::find_if(entities.begin(), entities.end(), [id](const Components& t) {
		return std::get<Entity>(t).GetId() == id;
	});
	if (entity_exists != / == entities.end()) { // entity found / not found in cache
		// Do stuff to cache
	}
	*/
}

} // namespace ecs

namespace std {

// Custom hashing function for ecs::Entity class allows for use of unordered maps with entities as keys
template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& k) const {
		using std::size_t;
		using std::hash;

		// Compute individual hash values for first,
		// second and third and combine them using XOR
		// and bit shifting:

		return ((hash<ecs::EntityId>()(k.GetId())
				 ^ (hash<ecs::EntityVersion>()(k.GetVersion()) << 1)) >> 1)
			^ (hash<ecs::Manager*>()(k.GetManager()) << 1);
	}
};

} // namespace std