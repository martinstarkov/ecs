#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <cassert>
#include <any>


#include <string>

// TODO: Add a function called CreateComponent which will be called in AddComponent and generate a new ID if
// the AddComponent <T>::GetId() does not exist already, this will also resize the components vector and 
// grow them each by one

// TEMPORARY 
///*

#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

template <typename T>
static void resize(std::vector<T>& vector, const size_t amount, std::string print) {
	LOG_("Resizing vector from " << vector.size() << " to ");
	vector.resize(amount);
	LOG_(vector.size() << ", with ");
	LOG(print);
}

template <typename T, typename S>
static void resize(std::vector<T>& vector, const size_t amount, const S& value, std::string print) {
	LOG_("Resizing vector from " << vector.size() << " to ");
	vector.resize(amount, value);
	LOG_(vector.size() << ", with ");
	LOG(print);
}

//*/ 
// END OF TEMPORARY

namespace ecs {

	using EntityId = int64_t;
	using ComponentId = int64_t;
	using SystemId = int16_t;
	using ComponentMask = std::vector<bool>;
	constexpr size_t STARTING_ENTITY_COUNT = 100;

	// Component Wrapper
	template <typename T>
	class ComponentContainer {
	public:
		ComponentContainer(T&& component) : component_(component) {}
		ComponentContainer(ComponentContainer&& other) = default;
		ComponentContainer& operator=(ComponentContainer&& other) = default;
		ComponentContainer(const ComponentContainer&) = default;
		ComponentContainer& operator=(const ComponentContainer&) = default;
		static void SetId(ComponentId id) {
			GetId(id);
		}
		// Start with invalid id
		static ComponentId GetId(ComponentId id = -1) {
			static ComponentId id_ = id;
			if (id != -1) {
				id_ = id;
			}
			return id_;
		}
		bool IsValidComponent() {
			return valid;
		}
		// No need to be able to set a component to valid = true as each ComponentContainer object can only be valid once
		void SetInvalidComponent() {
			valid = false;
		}
		T& GetComponent() {
			return component_;
		}
		friend std::ostream& operator<<(std::ostream& os, const ComponentContainer<T>& obj) {
			os << obj.component_;
			return os;
		}
	private:
		bool valid = true;
		T component_;
	};

	// System Wrapper
	template <typename T>
	class SystemContainer {
	public:
		SystemContainer(T&& system) : system_(system) {}
		SystemContainer(SystemContainer&& other) = default;
		SystemContainer& operator=(SystemContainer&& other) = default;
		SystemContainer(const SystemContainer&) = delete;
		SystemContainer& operator=(const SystemContainer&) = delete;
		static void SetId(SystemId id) {
			GetId(id);
		}
		// Start with invalid id
		static SystemId GetId(SystemId id = -1) {
			static SystemId id_ = id;
			if (id != -1) {
				id_ = id;
			}
			return id_;
		}
		T& GetSystem() {
			return system_;
		}
	private:
		T system_;
	};

	class ComponentStorage {
	public:
		void GrowByEntity(size_t new_entity_count) {
			for (auto& component : components_) {
				assert(new_entity_count > component.size());
				resize(component, new_entity_count, "component after entity grow");
			}
		}
		void GrowByComponent(size_t new_entity_count, size_t new_component_count) {
			std::vector<std::any> component;
			assert(new_entity_count > component.size());
			std::string string = std::string("#") + std::to_string(new_component_count) + std::string(" component after component grow");
			resize(component, new_entity_count, string);
			assert(new_component_count > components_.size());
			resize(components_, new_component_count, component, "component vector after component grow");
		}
		template <typename T, typename... TArgs>
		T& AddComponent(EntityId id, ComponentContainer<T> component) {
			auto& element = components_[ComponentContainer<T>::GetId()][id];
			element = component;
			return GetComponent<T>(id);
		}
		template<typename T>
		T& GetComponent(EntityId id) {
			return std::any_cast<ComponentContainer<T>&>(components_[ComponentContainer<T>::GetId()][id]).GetComponent();
		}
		template <typename T>
		bool HasComponent(EntityId id) {
			return std::any_cast<ComponentContainer<T>&>(components_[ComponentContainer<T>::GetId()][id]).IsValidComponent();
		}
		template <typename T>
		void RemoveComponent(EntityId id) {
			std::any_cast<ComponentContainer<T>&>(components_[ComponentContainer<T>::GetId()][id]).SetInvalidComponent();
		}
		size_t GetComponentCount() {
			return components_.size();
		}
	private:
		std::vector<std::vector<std::any>> components_;
	};

	class SystemStorage {
	public:
		/*template <typename T, typename ...TArgs>
		void AddSystem(TArgs&&... args) {
			systems_[SystemContainer<T>::GetId()] = SystemContainer<T>(T{ args... });
		}
		template<typename T>
		T& GetSystem() {
			return static_cast<T&>(systems_[SystemContainer<T>::GetId()]);
		}*/
	private:
		std::vector<std::any> systems_;
	};

	struct EntityData {
		bool alive;
		friend std::ostream& operator<<(std::ostream& os, const EntityData& obj) {
			os << "alive: " << obj.alive;
			return os;
		}
	};

	class Manager {
	public:
		Manager(size_t starting_entity_count = STARTING_ENTITY_COUNT) {
			//(GenerateComponentId(), ...);
			GrowByEntity(starting_entity_count);
		}
		Manager(Manager&& other) = default;
		Manager& operator=(Manager&& other) = default;
		Manager(const Manager&) = delete;
		Manager& operator=(const Manager&) = delete;
		//template <typename T, typename ...TArgs>
		//void AddSystem(TArgs&&... args) {
		//	// system.SetManager(this);
		//	system_storage_.AddSystem<T>(std::forward<TArgs>(args)...);
		//}
		void GrowByEntity(size_t new_entity_count) {
			assert(new_entity_count > entities_.size());
			resize(entities_, new_entity_count, "entity size after entity grow");
			component_storage_.GrowByEntity(new_entity_count);
			for (size_t i = max_entity_count_; i < new_entity_count; ++i) {
				EntityData new_entity;
				new_entity.alive = false;
				entities_[i] = new_entity;
			}
			max_entity_count_ = new_entity_count;
		}
		void GrowByComponent(size_t new_component_count) {
			assert(new_component_count > component_storage_.GetComponentCount());
			component_storage_.GrowByComponent(max_entity_count_, new_component_count);
		}
		bool IsAlive(EntityId id) {
			assert(id < static_cast<EntityId>(entities_.size()));
			return entities_[id].alive;
		}
		EntityId CreateEntity() {
			if (entity_count_ >= max_entity_count_) {
				GrowByEntity((max_entity_count_ + 10) * 2);
			}
			EntityId free_index(entity_count_++);
			assert(!IsAlive(free_index));
			EntityData new_entity;
			new_entity.alive = true;
			entities_[free_index] = new_entity;
			return free_index;
		}
		template <typename T>
		ComponentId CreateComponentId() {
			assert(!ComponentExists<T>());
			ComponentContainer<T>::SetId(component_count_);
			GrowByComponent(++component_count_);
			// size is one above count
			return component_count_ - 1;
		}
		template <typename T>
		bool ComponentExists() {
			if (ComponentContainer<T>::GetId() == -1) {
				return false;
			}
			return true;
		}
		template <typename T>
		ComponentId GetComponentId() {
			if (ComponentExists<T>()) {
				return ComponentContainer<T>::GetId();
			}
			return CreateComponentId<T>();
		}
		template <typename T, typename... TArgs>
		T& AddComponent(EntityId id, TArgs&&... args) {
			GetComponentId<T>();
			return component_storage_.AddComponent<T>(id, ComponentContainer<T>({ std::forward<TArgs>(args)... }));
		};
		template <typename T>
		bool HasComponent(EntityId id) {
			// Component has been added at least once to any entity
			if (ComponentExists<T>()) {
				return component_storage_.HasComponent<T>(id);
			}
			return false;
		}
		template <typename T>
		T& GetComponent(EntityId id) {
			assert(ComponentExists<T>());
			assert(HasComponent<T>(id));
			return component_storage_.GetComponent<T>(id);
		}
		template <typename T>
		void RemoveComponent(EntityId id) {
			assert(ComponentExists<T>());
			component_storage_.RemoveComponent<T>(id);
		}
	private:
		std::size_t max_entity_count_ = 0;
		std::size_t entity_count_ = 0;
		std::size_t component_count_ = 0;
		std::size_t system_count_ = 0;
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