#pragma once

#include <vector> // std::vector
#include <cstdlib> // std::size_t
#include <cstdint> // std::uint64_t, std::uint32_t
#include <type_traits> // std::is_constructible_v, std::is_destructible_v
#include <cassert> // assert

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

constexpr Version null_version{ 0 };

class BasePool {
public:
	virtual BasePool* Clone() const = 0;
	virtual void VirtualRemove(const DataIndex entity) = 0;
};

// Object which allows for contiguous storage of components of a single type (with runtime addition!).
template <typename TComponent>
class Pool : public BasePool {
private:
	using SparseIndex = std::uint32_t;
	using DenseIndex = std::int32_t;
	constexpr static DenseIndex INVALID_INDEX = -1;
public:
	Pool() = default;
	Pool(const std::vector<DenseIndex>& sparse_set, const std::vector<SparseIndex>& dense_set, const std::vector<TComponent>& components) : sparse_set_{ sparse_set }, dense_set_{ dense_set }, components_{ components } {}
	~Pool() = default;
	Pool(const Pool&) = default;
	Pool& operator=(const Pool&) = default;
	Pool(Pool&&) = default;
	Pool& operator=(Pool&&) = default;
	
	virtual void VirtualRemove(const DataIndex entity) override final {
		Remove(entity);
	}

	virtual BasePool* Clone() const override final {
		return new Pool<TComponent>(sparse_set_, dense_set_, components_);
	}

	template <typename ...TArgs>
	TComponent& Add(const DataIndex id, TArgs&&... args) {
		if (id < sparse_set_.size()) {
			auto dense_index = sparse_set_[id];
			if (dense_index != INVALID_INDEX && static_cast<std::size_t>(dense_index) < dense_set_.size()) {
				dense_set_[dense_index] = id;
				return *components_.emplace(components_.begin() + dense_index, std::forward<TArgs>(args)...);
			}
		} else {
			sparse_set_.resize(static_cast<std::size_t>(id) + 1, INVALID_INDEX);
		}
		sparse_set_[id] = dense_set_.size();
		dense_set_.emplace_back(id);
		return components_.emplace_back(std::forward<TArgs>(args)...);
	}

	bool Remove(const DataIndex id) {
		if (id < sparse_set_.size()) {
			auto& dense_index = sparse_set_[id];
			if (dense_index != INVALID_INDEX && static_cast<std::size_t>(dense_index) < dense_set_.size()) {
				if (dense_set_.size() > 1) {
					// If removing last element of sparse set, swap not required.
					if (dense_index == sparse_set_.back()) {
						dense_index = INVALID_INDEX;
						auto first_valid_index = std::find_if(sparse_set_.rbegin(), sparse_set_.rend(), [](DenseIndex index) { return index != INVALID_INDEX; });
						sparse_set_.erase(first_valid_index.base() + 1, sparse_set_.end());
					} else {
						auto last_sparse_index = dense_set_.back();
						std::swap(dense_set_[dense_index], dense_set_.back());
						assert(static_cast<std::size_t>(dense_index) < components_.size());
						std::swap(components_[dense_index], components_.back());
						sparse_set_[last_sparse_index] = dense_index;
						dense_index = INVALID_INDEX;
					}
					dense_set_.pop_back();
					components_.pop_back();
				} else {
					dense_set_.clear();
					sparse_set_.clear();
					components_.clear();
				}
				return true;
			}
		}
		return false;
	}

	const TComponent& Get(const DataIndex id) const {
		assert(id < sparse_set_.size());
		auto dense_index = sparse_set_[id];
		assert(dense_index != INVALID_INDEX);
		assert(static_cast<std::size_t>(dense_index) < components_.size());
		return components_[dense_index];
	}

	TComponent& Get(const DataIndex id) {
		return const_cast<TComponent&>(static_cast<const Pool&>(*this).Get(id));
	}

	bool Has(const DataIndex id) const {
		return id < sparse_set_.size() && sparse_set_[id] != INVALID_INDEX;
	}
private:
	std::vector<DenseIndex> sparse_set_;
	std::vector<SparseIndex> dense_set_;
	std::vector<TComponent> components_;
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
	bool operator==(const HandleData & other) const {
		return entity_index == other.entity_index && counter == other.counter;
	}
	bool operator!=(const HandleData & other) const {
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
		auto& component_object = pool->Add(entity, std::forward<TArgs>(args)...);

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
		return pool->Get(entity);
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
			bool removed = pool->Remove(entity);
			if (removed) {
				//ComponentChange(id, component_id, loop_entity);
			}
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