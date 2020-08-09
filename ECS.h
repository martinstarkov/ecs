#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <cassert>

namespace ecs {

	using EntityId = int64_t;
	using ComponentId = int64_t;
	using SystemId = int16_t;

	constexpr size_t STARTING_ENTITY_COUNT = 100;
	
	// Inlines which generate new component / system ids based on unique types
	inline ComponentId GetNewComponentId() {
		static ComponentId next_id = 0;
		return next_id++;
	}
	template <typename T> inline ComponentId GetComponentId() {
		static ComponentId id = GetNewComponentId();
		return id;
	}
	inline SystemId GetNewSystemId() {
		static SystemId next_id = 0;
		return next_id++;
	}
	template <typename T> inline SystemId GetSystemId() {
		static SystemId id = GetNewSystemId();
		return id;
	}

	// Pure virtual interface for ComponentWrappers
	class BaseComponent {
	public:
	};

	// Pure virtual interface for SystemWrappers
	class BaseSystem {
	public:
	};

	// Component Wrapper
	template <typename T>
	class ComponentContainer : public BaseComponent {
	public:
		ComponentContainer(T&& data) : data_(data) {}
		ComponentContainer(ComponentContainer&& other) = default;
		ComponentContainer& operator=(ComponentContainer&& other) = default;
		ComponentContainer(const ComponentContainer&) = delete;
		ComponentContainer& operator=(const ComponentContainer&) = delete;
		static ComponentId GetId() {
			return GetComponentId<T>();
		}
		T& GetComponent() {
			return data_;
		}
	private:
		T data_;
	};

	// System Wrapper
	template <typename T>
	class SystemContainer : public BaseSystem {
	public:
		SystemContainer(T system) : system_(std::move(system)) {}
		static SystemId GetId() {
			return GetSystemId<T>();
		}
	private:
		T system_;
	};

	// Handle for Components
	template <typename T>
	class Component {
	public:

	private:

	};

	// Handle for Systems
	template <typename T>
	class System {
	public:

	private:

	};

	class ComponentStorage {
	public:
		void Grow(size_t new_capacity) {
			for (auto& vector : components_) {
				vector.resize(new_capacity);
			}
		}
		template <typename T, typename... TArgs>
		T& AddComponent(EntityId id, TArgs&&... args) {
			if (ComponentContainer<T>::GetId() >= Size()) {
				components_.resize(1);
			}
			Grow(1);
			assert(ComponentContainer<T>::GetId() < Size());
			assert(id < static_cast<EntityId>(components_[ComponentContainer<T>::GetId()].size()));
			components_[ComponentContainer<T>::GetId()][id] = std::make_unique<ComponentContainer<T>>(T{ std::forward<TArgs>(args)... });
			return GetComponent<T>(id);
		}
		template<typename T>
		T& GetComponent(EntityId index) {
			return static_cast<ComponentContainer<T>*>(components_[ComponentContainer<T>::GetId()][index].get())->GetComponent();
		}
		size_t Size_t() {
			return components_.size();
		}
		ComponentId Size() {
			return static_cast<ComponentId>(Size_t());
		}
	private:
		std::vector<std::vector<std::unique_ptr<BaseComponent>>> components_;
	};

	class SystemStorage {
	public:
		template <typename T, typename ...TArgs>
		void AddSystem(TArgs&&... args) {
			systems_[SystemContainer<T>::getId()] = std::make_unique<SystemContainer<T>>(T{ args... });
		}
		template<typename T>
		T& GetSystem() {
			return static_cast<T&>(systems_[SystemContainer<T>::GetId()]);
		}
	private:
		std::vector<BaseSystem> systems_;
	};

	struct EntityData {
		// add signature bitset here
		std::vector<bool> component_mask;
		bool alive;
	};

	class Manager {
	public:
		Manager(size_t starting_entity_count = STARTING_ENTITY_COUNT) { Grow(starting_entity_count); }
		Manager(Manager&& other) = default;
		Manager& operator=(Manager&& other) = default;
		Manager(const Manager&) = delete;
		Manager& operator=(const Manager&) = delete;
		template <typename T, typename ...TArgs>
		void AddSystem(TArgs&&... args) {
			// system.SetManager(this);
			system_storage_.AddSystem<T>(std::forward<TArgs>(args)...);
		}
		void Grow(size_t new_capacity) {
			assert(new_capacity > capacity_);
			entities_.resize(new_capacity);
			component_storage_.Grow(new_capacity);
			for (size_t i = capacity_; i < new_capacity; ++i) {
				EntityData e;
				e.alive = false;
				e.component_mask.resize(component_storage_.Size_t(), false);
				entities_[i] = e;
			}
			capacity_ = new_capacity;
		}
		void GrowIfNecessary() {
			if (capacity_ > size_) return;
			Grow((capacity_ + 10) * 2);
		}
		bool IsAlive(EntityId index) {
			assert(index < static_cast<EntityId>(entities_.size() - 1));
			return entities_[index].alive;
		}
		EntityId CreateIndex() {
			GrowIfNecessary();
			EntityId free_index(size_++);
			assert(!IsAlive(free_index));
			EntityData e;
			e.alive = true;
			e.component_mask.resize(component_storage_.Size_t(), false);
			entities_[free_index] = e;
			return free_index;
		}
		template <typename T, typename... TArgs>
		T& AddComponent(EntityId id, TArgs&&... args) {
			// component.setManager()
			if (component_storage_.Size() - 1 < ComponentContainer<T>::GetId()) {
				entities_[id].component_mask.resize(component_storage_.Size_t() + 1, false);
			}
			assert(static_cast<ComponentId>(entities_[id].component_mask.size()) >= ComponentContainer<T>::GetId());
			entities_[id].component_mask[ComponentContainer<T>::GetId()] = true;
			return component_storage_.AddComponent<T>(id, std::forward<TArgs>(args)...);
		}
		template <typename T>
		bool HasComponent(EntityId id) {
			if (ComponentContainer<T>::GetId() < entities_[id].component_mask.size()) {
				return entities_[id].component_mask[ComponentContainer<T>::GetId()];
			}
			return false;
		}
		template <typename T>
		T& GetComponent(EntityId id) {
			return component_storage_.GetComponent<T>(id);
		}
		template <typename T>
		void RemoveComponent(EntityId id) {
			assert(entities_[id].component_mask.size() > 0);
			entities_[id].component_mask[ComponentContainer<T>::getId()] = false;
		}
	private:
		size_t capacity_{ 0 }, size_{ 0 };
		std::vector<EntityData> entities_;
		SystemStorage system_storage_;
		ComponentStorage component_storage_;
	};

	class Entity {
	public:
		Entity(EntityId id, Manager& manager) : id_(id), manager_(manager) {}
		Entity(const Entity& other) = default;
		Entity& operator=(const Entity& other) = default;
		Entity(Entity&& other) = default;
		Entity& operator=(Entity&& other) = default;
		EntityId getId() {
			return id_;
		}
		// Handle methods
		template <typename T, typename ...TArgs>
		T& AddComponent(TArgs&&... args) {
			return manager_.AddComponent<T>(id_, std::forward<TArgs>(args)...);
		}
		template <typename T>
		T& GetComponent() {
			return manager_.GetComponent<T>(id_);
		}
		template <typename T>
		bool HasComponent() {
			return manager_.HasComponent<T>(id_);
		}
		template <typename T>
		void RemoveComponent() {
			manager_.RemoveComponent<T>(id_);
		}
	private:
		EntityId id_;
		Manager& manager_;
	};

} // namespace ecs