#pragma once

#include <vector> // std::vector
#include <cstdlib> // std::size_t
#include <cstdint> // std::uint64_t, std::uint32_t
#include <type_traits> // std::is_constructible_v, std::is_destructible_v
#include <cassert> // assert
#include <deque> // std::deque
#include <iostream> // std::err for exception handling

// TODO: TEMPORARY: ONLY FOR TESTS
class ManagerBasics;
class EntityBasics;

namespace ecs {

namespace internal {

using EntityIndex = std::uint32_t;
using DataIndex = std::uint32_t;
using HandleIndex = std::uint32_t;
using ComponentId = std::uint32_t;
using SystemId = std::uint32_t;
using Version = std::uint32_t;
using EntityId = DataIndex;
using Offset = std::int64_t;

constexpr Version null_version{ 0 };
constexpr Offset INVALID_OFFSET = -1;

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual void VirtualRemove(const EntityId entity) = 0;
};

// Object which allows for contiguous storage of components of a single type (with runtime addition!).
// Object which allows for contiguous storage of components of a single type (with runtime addition!).
template <typename TComponent>
class Pool : public BasePool {
// Indicates a nonexistent component in a pool.
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
			offsets_.resize(static_cast<std::size_t>(entity) + 1, INVALID_OFFSET);
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

struct EntityData {
	EntityData() = default;
	~EntityData() = default;
	EntityData(const EntityData&) = default;
	EntityData& operator=(const EntityData&) = default;
	EntityData(EntityData&&) = default;
	EntityData& operator=(EntityData&&) = default;
	bool operator==(const EntityData& other) const {
		return data_index == other.data_index && handle_index == other.handle_index && alive == other.alive;
	}
	bool operator!=(const EntityData& other) const {
		return !operator==(other);
	}

	bool alive{ false };
	DataIndex data_index{ 0 };
	HandleIndex handle_index{ 0 };
};

struct HandleData {

	HandleData() = default;
	~HandleData() = default;
	HandleData(const HandleData&) = default;
	HandleData& operator=(const HandleData&) = default;
	HandleData(HandleData&&) = default;
	HandleData& operator=(HandleData&&) = default;
	bool operator==(const HandleData& other) const {
		return entity_index == other.entity_index && counter == other.counter;
	}
	bool operator!=(const HandleData& other) const {
		return !operator==(other);
	}
	EntityIndex entity_index{ 0 };
	Version counter{ null_version };
};

class BaseSystem {
public:
	virtual internal::BaseSystem* Clone() const = 0;
};

} // namespace internal

struct Entity;

class Manager {
public:
	Manager() = default;
	~Manager() {
		size_ = 0;
		size_next_ = 0;
		entities_.clear();
		for (auto pool : component_pools_) {
			delete pool;
		}
		for (auto system : systems_) {
			delete system;
		}
		component_pools_.clear();
		systems_.clear();
	}
	Manager(Manager&& other) noexcept : size_{ other.size_ }, size_next_{ other.size_next_ }, entities_{ std::exchange(other.entities_, {}) }, component_pools_{ std::exchange(other.component_pools_, {}) }, systems_{ std::exchange(other.systems_, {}) }, handles_{ std::exchange(other.handles_, {}) } {
		other.size_ = 0;
		other.size_next_ = 0;
	}
	Manager& operator=(Manager&& other) noexcept {
		for (auto pool : component_pools_) {
			delete pool;
		}
		for (auto system : systems_) {
			delete system;
		}

		size_ = other.size_;
		size_next_ = other.size_next_;
		entities_ = std::exchange(other.entities_, {});
		handles_ = std::exchange(other.handles_, {});
		component_pools_ = std::exchange(other.component_pools_, {});
		systems_ = std::exchange(other.systems_, {});

		other.size_ = 0;
		other.size_next_ = 0;
	}
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	bool operator==(const Manager& other) const {
		return size_ == other.size_ && size_next_ == other.size_next_ && entities_ == other.entities_ && component_pools_ == other.component_pools_ && handles_ == other.handles_;
	}
	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}
	Entity CreateEntity();

	void Refresh() {
		if (size_next_ == 0) {
			size_ = 0;
			return;
		}
		size_ = size_next_ = RefreshImpl();
	}

	std::size_t GetEntityCount() {
		return size_;
	}
	/*
	std::size_t GetDeadEntityCount() {
		return dead_list_.size();
	}*/

	void Clear() {
		for (internal::DataIndex i{ 0 }; i < static_cast<internal::DataIndex>(entities_.capacity()); ++i) {
			auto& entity{ entities_[i] };
			entity.data_index = i;
			entity.alive = false;
			entity.handle_index = i;

			auto& handle{ handles_[i] };
			handle.counter = 0;
			handle.entity_index = i;
		}

		size_ = size_next_ = 0;
	}
	std::vector<Entity> GetEntities();
private:
	// Credit for the refresh algorithm goes to Vittorio Romeo for his talk on entity component systems at CppCon 2015.
	internal::EntityIndex RefreshImpl() {
		internal::EntityIndex dead{ 0 };
		internal::EntityIndex alive{ size_next_ - 1 };

		while (true) {
			for (; true; ++dead) {
				if (dead > alive) return dead;
				if (!entities_[dead].alive) break;
			}
			for (; true; --alive) {
				auto& entity = entities_[alive];
				if (entity.alive) break;
				++handles_[entity.handle_index].counter;
				RemoveComponents(entity.data_index);
				if (alive <= dead) return dead;
			}
			auto& alive_entity = entities_[alive];
			auto& dead_entity = entities_[dead];
			assert(alive_entity.alive);
			assert(!dead_entity.alive);

			std::swap(alive_entity, dead_entity);

			handles_[dead_entity.handle_index].entity_index = dead;

			++handles_[alive_entity.handle_index].counter;
			handles_[alive_entity.handle_index].entity_index = alive;

			RemoveComponents(alive_entity.data_index);
			++dead; --alive;
		}

		return dead;
	}

	template <typename TComponent>
	internal::Pool<TComponent>* GetPool(const internal::ComponentId component) const {
		assert(component < component_pools_.size());
		return static_cast<internal::Pool<TComponent>*>(component_pools_[component]);
	}

	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(const internal::DataIndex entity, TArgs&&... args) {
		static_assert(std::is_constructible_v<TComponent, TArgs...>, "Cannot add component with given constructor argument list");
		static_assert(std::is_destructible_v<TComponent>, "Cannot add component without valid destructor");
		auto component = GetComponentId<TComponent>();
		if (component >= component_pools_.size()) {
			component_pools_.resize(component + 1);
		}
		auto pool = GetPool<TComponent>(component);
		bool new_component = pool == nullptr;
		if (new_component) {
			pool = new internal::Pool<TComponent>();
			component_pools_[component] = pool;
		}
		assert(pool != nullptr);
		auto& component_object = *pool->Add(entity, std::forward<TArgs>(args)...);

		if (new_component) {
			// ComponentChange(entity, component, loop_entity);
		}
		return component_object;
	}
	template <typename TComponent>
	TComponent& GetComponent(const internal::DataIndex entity) {
		auto component = GetComponentId<TComponent>();
		auto pool = GetPool<TComponent>(component);
		assert(pool != nullptr);
		return *pool->Get(entity);
	}
	template <typename TComponent>
	bool HasComponent(const internal::DataIndex entity) const {
		auto component = GetComponentId<TComponent>();
		auto pool = GetPool<TComponent>(component);
		return pool != nullptr && pool->Has(entity);
	}
	template <typename ...TComponents>
	bool HasComponents(const internal::DataIndex entity) const {
		return { (HasComponent<TComponents>(entity) && ...) };
	}

	template <typename TComponent>
	void RemoveComponent(const internal::DataIndex entity) {
		auto component = GetComponentId<TComponent>();
		auto pool = GetPool<TComponent>(component);
		if (pool != nullptr) {
			pool->Remove(entity);
			//if (removed) {
			//	//ComponentChange(id, component_id, loop_entity);
			//}
		}
	}
	template <typename ...TComponents>
	void RemoveComponents(const internal::DataIndex entity) {
		(RemoveComponent<TComponents>(entity), ...);
	}

	void RemoveComponents(const internal::DataIndex entity) {
		for (auto& pool : component_pools_) {
			if (pool) {
				pool->VirtualRemove(entity);
			}
		}
	}

	const internal::EntityData& GetEntityData(const internal::EntityIndex entity_index) const {
		assert(entity_index < entities_.size());
		return entities_[entity_index];
	}
	internal::EntityData& GetEntityData(const internal::EntityIndex entity_index) {
		return const_cast<internal::EntityData&>(static_cast<const Manager&>(*this).GetEntityData(entity_index));
	}

	const internal::HandleData& GetHandleData(const internal::HandleIndex handle_index) const {
		assert(handle_index < handles_.size());
		return handles_[handle_index];
	}
	internal::HandleData& GetHandleData(const internal::HandleIndex handle_index) {
		return const_cast<internal::HandleData&>(static_cast<const Manager&>(*this).GetHandleData(handle_index));
	}

	bool IsValidHandle(const internal::HandleData& handle, const internal::Version counter) const {
		return handle.counter == counter;
	}


	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class ManagerBasics;
	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class EntityBasics;

	friend struct Entity;

	template <typename TComponent>
	static internal::ComponentId GetComponentId() {
		static internal::ComponentId component = GetComponentCount()++;
		return component;
	}
	static internal::ComponentId& GetComponentCount() { static internal::ComponentId id{ 0 }; return id; }
	static internal::SystemId& GetSystemCount() { static internal::SystemId id{ 0 }; return id; }

	internal::EntityIndex size_{ 0 };
	internal::EntityIndex size_next_{ 0 };
	std::vector<internal::EntityData> entities_;
	std::vector<internal::HandleData> handles_;
	std::vector<internal::BasePool*> component_pools_;
	std::vector<internal::BaseSystem*> systems_;
};

struct Entity {
	Entity() = default;
	Entity(Manager* manager, const internal::HandleIndex handle_index, const internal::Version counter) : manager_{ manager }, handle_index_{ handle_index }, counter_{ counter } {}
	~Entity() = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	bool operator==(const Entity& other) const {
		return handle_index_ == other.handle_index_ && counter_ == other.counter_ && manager_ == other.manager_;
	}
	bool operator!=(const Entity& other) const {
		return !operator==(other);
	}
	template <typename TComponent, typename ...TArgs>
	TComponent& AddComponent(TArgs&&... constructor_args) {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		const auto& entity = manager_->GetEntityData(handle.entity_index);
		return manager_->AddComponent<TComponent>(entity.data_index, std::forward<TArgs>(constructor_args)...);
	}
	template <typename TComponent>
	TComponent& GetComponent() {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		const auto& entity = manager_->GetEntityData(handle.entity_index);
		return manager_->GetComponent<TComponent>(entity.data_index);
	}
	template <typename TComponent>
	void RemoveComponent() {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		const auto& entity = manager_->GetEntityData(handle.entity_index);
		manager_->RemoveComponent<TComponent>(entity.data_index);
	}
	template <typename ...TComponents>
	void RemoveComponents() {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		const auto& entity = manager_->GetEntityData(handle.entity_index);
		manager_->RemoveComponents<TComponents...>(entity.data_index);
	}
	template <typename TComponent>
	bool HasComponent() const {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		const auto& entity = manager_->GetEntityData(handle.entity_index);
		return manager_->HasComponent<TComponent>(entity.data_index);
	}
	template <typename ...TComponents>
	bool HasComponents() const {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		const auto& entity = manager_->GetEntityData(handle.entity_index);
		return manager_->HasComponents<TComponents...>(entity.data_index);
	}
	void Destroy() {
		assert(manager_ != nullptr);
		const auto& handle = manager_->GetHandleData(handle_index_);
		assert(manager_->IsValidHandle(handle, counter_));
		auto& entity = manager_->GetEntityData(handle.entity_index);
		entity.alive = false;
	}
private:

	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class EntityBasics;

	friend class Manager;
	friend struct NullEntity;
	Manager* const manager_{ nullptr };
	const internal::HandleIndex handle_index_{ 0 };
	internal::Version counter_{ internal::null_version };
};

struct NullEntity {
	operator Entity() const {
		return Entity{ nullptr, 0, internal::null_version };
	}
	constexpr bool operator==(const NullEntity&) const {
		return true;
	}
	constexpr bool operator!=(const NullEntity&) const {
		return false;
	}
	bool operator==(const Entity& entity) const {
		return entity.counter_ == internal::null_version;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}
};

bool operator==(const Entity& entity, const NullEntity& null_entity) {
	return null_entity == entity;
}
bool operator!=(const Entity& entity, const NullEntity& null_entity) {
	return !(null_entity == entity);
}

inline constexpr NullEntity null{};

inline Entity Manager::CreateEntity() {
	auto capacity = entities_.capacity();
	if (capacity <= size_next_) {
		auto new_capacity = (capacity + 10) * 2;
		assert(new_capacity > capacity);
		entities_.resize(new_capacity);
		handles_.resize(new_capacity);
		for (internal::DataIndex i{ static_cast<internal::DataIndex>(capacity) }; i < static_cast<internal::DataIndex>(new_capacity); ++i) {
			auto& entity{ entities_[i] };

			entity.data_index = i;
			entity.alive = false;
			entity.handle_index = i;

			auto& handle{ handles_[i] };

			handle.counter = internal::null_version;
			handle.entity_index = i;
		}
	}
	internal::EntityIndex free_index{ size_next_++ };
	auto& entity{ entities_[free_index] };
	assert(!entity.alive);
	entity.alive = true;
	auto& handle = handles_[entity.handle_index];
	handle.entity_index = free_index;
	return Entity{ this, entity.handle_index, handle.counter };
}

inline std::vector<Entity> Manager::GetEntities() {
	std::vector<Entity> entities;
	entities.reserve(size_);
	// Cycle through all manager entities.
	for (std::size_t i = 0; i < size_; ++i) {
		auto& entity = entities_[i];
		entities.emplace_back(this, entity.handle_index, handles_[entity.handle_index].counter);
	}
	return entities;
}

} // namespace ecs