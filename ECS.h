#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <any>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <type_traits>
#include <initializer_list>
#include <variant>
#include <sstream>
#include <cmath>

#include <string>

// TODO: Add license at top of file when you get there

// TODO: Add a function called CreateComponent which will be called in AddComponent and generate a new ID if
// the AddComponent <T>::GetId() does not exist already, this will also resize the components vector and 
// grow them each by one

// TEMPORARY 
///*

#define DEBUG

#ifdef DEBUG
	#define EXPAND( condition ) condition
	#define assertion(condition, ...) { if (!(condition)) { internal::Assert(condition, #condition, __FILE__, __LINE__, { __VA_ARGS__ }); } }
// Note: add parentheses when condition contains non argument commas, e.g. when using metaprogramming structs
	#define assert(...) EXPAND(assertion(__VA_ARGS__))
	#define LOG(x) { std::cout << x << std::endl; }
	#define LOG_(x) { std::cout << x; }
#else
	#define assert(...)
	#define LOG(x)
	#define LOG_(x)
#endif

// Do not use internal functions outside API
namespace internal {

// Combine C++ and C strings into one string from initializer list
std::string concatenate(std::initializer_list<std::variant<const char*, std::string>> strings) {
	std::string combined;
	std::size_t total = 0;
	// count size of all strings combined so it can be reserved
	for (auto& string : strings) {
		if (std::holds_alternative<const char*>(string)) {
			total += strlen(std::get<const char*>(string));
		} else {
			total += std::get<std::string>(string).size();
		}
	}
	combined.reserve(total);
	// append strings together
	for (auto& string : strings) {
		if (std::holds_alternative<const char*>(string)) {
			combined.append(std::get<const char*>(string));
		} else {
			combined.append(std::get<std::string>(string));
		}
	}
	return combined;
}

// Print a formatted assertion fail to the console
// First element of each pair is the field name, second is the field value
void ConsoleAssertion(std::ostream& os, std::string title, std::pair<std::string, std::string> condition, std::pair<std::string, std::string> file, std::pair<std::string, std::string> line, std::pair<std::string, std::string> message = {}) {
	// add newline to separate from other console messages
	os << std::endl;
	// combine fields names and values
	condition.first += condition.second + "\n";
	file.first += file.second + "\n";
	line.first += line.second + "\n";
	// unlike the other fields, we only want message to appear when there is one, therefore print and append to .second, otherwise it  will be an empty string
	if (message.second.size() > 0) {
		message.second = message.first + message.second + "\n";
	}
	// find longest line in assertion
	std::size_t max_size = std::max({ condition.first.size(), file.first.size(), line.first.size(), message.second.size() }) - 1;
	// must be divisible by 2 for top divider
	if (max_size % 2 == 1) {
		++max_size;
	}
	std::string bottom_divider = std::string(max_size, '-');
	// surround title with top dividers
	std::string top_divider = std::string((max_size - title.size()) / 2, '-');
	os << top_divider << title << top_divider << "\n";
	// message.second so that empty do not appear whatsoever
	os << condition.first << file.first << line.first << message.second;
	os << bottom_divider << std::endl;
}

void Assert(bool assertion, const char* condition, const char* file, int line, std::initializer_list<std::variant<const char*, std::string>> msg) {
	ConsoleAssertion(std::cerr, " Assertion Failed ", { "Condition: ", condition }, { "File: ", file }, { "Line: ", std::to_string(line) }, { "Message: ", concatenate(msg) });
	abort();
}
} // namespace internal

//*/ 
// END OF TEMPORARY

// Entity Component System
namespace ecs {

using ComponentId = int64_t;
using ComponentIndex = int64_t;
using EntityId = int64_t;

template <typename T, typename ...Ts>
std::vector<T>& AccessTuple(std::tuple<Ts...>& tuple) {
	// no testing as std::get prevents compilation unless type T exists in tuple
	return std::get<std::vector<T>&>(tuple);
}

// Thanks to Andy G https://gieseanw.wordpress.com/2017/05/03/a-true-heterogeneous-container-in-c/ for heterogeneous container concept

struct ComponentStorage {
public:
	ComponentStorage() = delete;
	ComponentStorage(std::size_t storage_index) : storage_index_(storage_index) {
		component_counts_.resize(component_counts_.size() + 1, 0);
	}
	ComponentStorage(const ComponentStorage&) = delete;
	ComponentStorage& operator=(const ComponentStorage&) = delete;
	ComponentStorage(ComponentStorage&&) = delete;
	ComponentStorage& operator=(ComponentStorage&&) = delete;
	template <class T>
	void PossibleNewComponent() {
		if (storage_index_ == storage_<T>.size()) {
			storage_<T>.resize(storage_<T>.size() + 1);
			ComponentId& count = component_counts_[storage_index_];
			ids_<T>.resize(ids_<T>.size() + 1, -1);
			ids_<T>[storage_index_] = count;
			// note: do not erase entry here as that would require shifting all storage indexes above the current one by -1
			clear_functions_.emplace_back([](const std::size_t storage_index) { storage_<T>[storage_index].clear(); });
			++count;
		}
	}
	template <class T>
	void Reserve(std::size_t additional_amount) {
		PossibleNewComponent<T>();
		assert(storage_index_ < storage_<T>.size());
		auto& v = storage_<T>[storage_index_];
		v.reserve(v.capacity() + additional_amount);
	}
	// Make sure there exists a matching constructor to the passed arguments
	template <class T, class ...TArgs>
	std::pair<std::pair<ComponentId, ComponentIndex>, T&> EmplaceBack(TArgs&&... args) {
		PossibleNewComponent<T>();
		auto& v = storage_<T>[storage_index_];
		// double size every time capacity is reached
		if (v.size() >= v.capacity()) {
			v.reserve(v.capacity() * 2);
		}
		v.emplace_back(std::forward<TArgs>(args)...);
		std::size_t component_index = v.size() - 1;
		// TODO: revisit this return
		return { { GetComponentId<T>(), component_index }, v[component_index] };
	}
	template <class T>
	std::vector<T>& GetComponentVector() const {
		assert(storage_index_ < storage_<T>.size());
		return storage_<T>[storage_index_];
	}
	template <class ...Ts>
	std::tuple<std::vector<Ts>&...> GetComponentVectors() const {
		return std::forward_as_tuple(GetComponentVector<Ts>()...);
	}
	template <class T>
	ComponentId GetComponentId() const {
		assert(storage_index_ < ids_<T>.size());
		return ids_<T>[storage_index_];
	}
	template <class T>
	T& GetComponent(std::size_t index) const {
		std::vector<T>& v = GetComponentVector<T>();
		assert(index < v.size());
		return v[index];
	}
	template <class T>
	T& GetComponent(std::vector<T>& component_vector, std::size_t index) const {
		assert(index < v.size());
		return component_vector[index];
	}
	template <class ...Ts>
	std::tuple<Ts&...> GetComponents(std::size_t index) const {
		return std::forward_as_tuple(GetComponent<Ts>(index)...);
	}
	// TODO: Revisit this method
	template <class T>
	bool HasComponent(std::size_t index) const {
		return index < GetComponentVector<T>().size();
	}
	template <class T>
	std::size_t Count() const {
		if (storage_index_ < storage_<T>.size()) {
			return storage_<T>[storage_index_].size();
		}
		return 0;
	}
	std::size_t Size() {
		return component_counts_[storage_index_];
	}
	~ComponentStorage() {
		for (auto&& clear_function : clear_functions_) {
			clear_function(storage_index_);
		}
	}
private:
	std::size_t storage_index_;
	template <class T>
	static std::vector<std::vector<T>> storage_;
	template <class T>
	static std::vector<ComponentId> ids_;
	static std::vector<ComponentId> component_counts_;
	std::vector<std::function<void(const std::size_t)>> clear_functions_;
};
// Static ComponentStorage variables
template <class T>
std::vector<std::vector<T>> ComponentStorage::storage_;
template <class T>
std::vector<ComponentId> ComponentStorage::ids_;
std::vector<ComponentId> ComponentStorage::component_counts_;

class EntityData {
public:
	// -1 indicates component does not exist
	ComponentIndex GetComponentIndex(ComponentId id) const {
		for (auto it = components.begin(); it != components.end(); ++it) {
			if (it->first == id) {
				return it->second;
			}
		}
		return -1;
	}
	std::vector<std::pair<ComponentId, ComponentIndex>> components;
	bool alive = false;
};

class Manager {
public:
	Manager() : component_storage_(manager_id++) {}
	EntityId CreateEntity() {
		// RESIZE entities vector to double the size if capacity is reached
		if (entity_count_ >= static_cast<EntityId>(entities_.capacity())) {
			ResizeEntities(static_cast<EntityId>(entities_.capacity() + 2));
		}
		//assert(entity_count_ < static_cast<EntityId>(entities_.size()), "Entities vector was not RESIZED to a big enough size (perhaps you reserved by accident?)");
		EntityId free_index(entity_count_++);
		//assert(!IsAlive(free_index));
		EntityData new_entity;
		new_entity.alive = true;
		//assert(free_index < static_cast<EntityId>(entities_.size()), "Index ", std::to_string(free_index), " out of range ", std::to_string(static_cast<EntityId>(entities_.size())));
		entities_[free_index] = std::move(new_entity);
		return free_index;
	}
	void PrintEntityComponents(EntityId index) {
		for (auto& pair : entities_[index].components) {
			LOG(index << ":" << pair.first << "," << pair.second);
		}
	}
	template <class T, typename ...TArgs>
	T& AddComponent(EntityId index, TArgs&&... args) {
		//assert(HasEntity(index), "Could not find entity with id: ", std::to_string(index));
		// pair{ component index, reference to component }
		auto pair = component_storage_.EmplaceBack<T>(std::forward<TArgs>(args)...);
		//assert(index < static_cast<EntityId>(entities_.size()), "Index ", std::to_string(index), " out of range ", std::to_string(static_cast<EntityId>(entities_.size())));
		entities_[index].components.emplace_back(pair.first.first, pair.first.second);
		return pair.second;
	}
	template <class T>
	T& GetComponent(EntityId index) const {
		//assert(HasEntity(index), "Could not find entity with id: ", std::to_string(index));
		ComponentId id = component_storage_.GetComponentId<T>();
		//assert(index < static_cast<EntityId>(entities_.size()), "Index ", std::to_string(index), " out of range ", std::to_string(static_cast<EntityId>(entities_.size())));
		ComponentIndex component_index = entities_[index].GetComponentIndex(id);
		//assert(component_index != -1, "Entity ", std::to_string(index), " does not have ", typeid(T).name());
		return component_storage_.GetComponent<T>(component_index);
	}
	template <class T>
	T& GetComponent(std::vector<T>& vector, EntityId index) const {
		//assert(HasEntity(index), "Could not find entity with id: ", std::to_string(index));
		ComponentId id = component_storage_.GetComponentId<T>();
		//assert(index < static_cast<EntityId>(entities_.size()), "Index ", std::to_string(index), " out of range ", std::to_string(static_cast<EntityId>(entities_.size())));
		ComponentIndex component_index = entities_[index].GetComponentIndex(id);
		//assert(component_index != -1, "Entity ", std::to_string(index), " does not have ", typeid(T).name());
		return vector[component_index];
	}
	template <class T>
	bool HasComponent(EntityId index) {
		//assert(HasEntity(index));
		ComponentId id = component_storage_.GetComponentId<T>();
		ComponentIndex component_index = entities_[index].GetComponentIndex(id);
		return component_index != -1;
	}
	template <class ...Ts>
	std::tuple<std::vector<Ts>&...> GetComponentVectors() const {
		return component_storage_.GetComponentVectors<Ts...>();
	}
	template <class T>
	void ReserveComponent(std::size_t additional_amount) {
		component_storage_.Reserve<T>(additional_amount);
	}
	void ResizeEntities(std::size_t additional_amount) {
		entities_.resize(entities_.capacity() + additional_amount);
	}
	bool HasEntity(EntityId index) const {
		return index < entity_count_;
	}
	bool IsAlive(EntityId index) const {

		//assert(HasEntity(index), "Entity ", std::to_string(index), " is out of range ", std::to_string(entity_count_));
		//assert(index < static_cast<EntityId>(entities_.size()), "Index ", std::to_string(index), " out of range ", std::to_string(static_cast<EntityId>(entities_.size())));
		return entities_[index].alive;
	}
	template <typename T, typename S>
	inline void PrintTuple(S& tuple, EntityId index) const {
		T& component = GetComponent(ecs::AccessTuple<T>(tuple), index);
		LOG_(component << ", ");
	}
	template <typename ...Ts>
	void PrintComponents() const {
		auto tuple = component_storage_.GetComponentVectors<Ts...>();
		for (EntityId i = 0; i < entity_count_; ++i) {
			LOG_("Entity " << i << " : ");
			(PrintTuple<Ts>(tuple, i), ...);
			LOG("");
		}
	}
	EntityId EntityCount() const {
		return entity_count_;
	}
private:
	EntityId entity_count_ = 0;
	ComponentStorage component_storage_;
	std::vector<EntityData> entities_;
	static std::size_t manager_id;
};
std::size_t Manager::manager_id = 0;

/*

using EntityId = int32_t;
using ComponentId = int32_t;
using ComponentMask = std::vector<bool>;
constexpr std::size_t STARTING_ENTITY_COUNT = 100;

// Make sure to wrap std::any_cast in std::any::has_value()
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

class ComponentStorage {
public:
	void GrowByEntity(std::size_t new_entity_count) {
		for (auto& component : components_) {
			assert(new_entity_count > component.size());
			resize(component, new_entity_count, "component after entity grow");
		}
	}
	void GrowByComponent(std::size_t new_entity_count, std::size_t new_component_count) {
		std::vector<std::any> component;
		assert(new_entity_count > component.size());
		std::string string = std::string("#") + std::to_string(new_component_count) + std::string(" component after component grow");
		resize(component, new_entity_count, string);
		assert(new_component_count > components_.size());
		resize(components_, new_component_count, component, "component vector after component grow");
	}
	template <typename T, typename... TArgs>
	T& AddComponent(EntityId id, ComponentContainer<T> component) {
		components_[ComponentContainer<T>::GetId()][id] = component;
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
	std::size_t GetComponentCount() {
		return components_.size();
	}
	// TEMPORARY
	template <typename T>
	void printComponent(std::vector<std::any>& component) {
		LOG_(typeid(T).name() << " (capacity=" << component.size() << "): ");
		EntityId counter = 0;
		for (auto& c : component) {
			LOG_(counter << ":");
			if (c.has_value()) {
				if (std::any_cast<ComponentContainer<T>&>(c).IsValidComponent()) {
					LOG_(std::any_cast<ComponentContainer<T>&>(c).GetComponent() << ", ");
				}
			}
			++counter;
		}
		LOG("");
	}
	template <typename ...Ts>
	void printComponents() {
		(printComponent<Ts>(components_[ComponentContainer<Ts>::GetId()]), ...);
	}
private:
	std::vector<std::vector<std::any>> components_;
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
	Manager(std::size_t starting_entity_count = STARTING_ENTITY_COUNT) {
		//(GenerateComponentId(), ...);
		GrowByEntity(starting_entity_count);
	}
	Manager(Manager&& other) = default;
	Manager& operator=(Manager&& other) = default;
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	void GrowByEntity(std::size_t new_entity_count) {
		assert(new_entity_count > entities_.size());
		resize(entities_, new_entity_count, "entity size after entity grow");
		component_storage_.GrowByEntity(new_entity_count);
		for (std::size_t i = max_entity_count_; i < new_entity_count; ++i) {
			EntityData new_entity;
			new_entity.alive = false;
			entities_[i] = new_entity;
		}
		max_entity_count_ = new_entity_count;
	}
	void GrowByComponent(std::size_t new_component_count) {
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
		EntityId free_index(static_cast<EntityId>(entity_count_++));
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
	ComponentStorage& GetComponentStorage() {
		return component_storage_;
	}
private:
	std::size_t max_entity_count_ = 0;
	std::size_t entity_count_ = 0;
	std::size_t component_count_ = 0;
	std::size_t system_count_ = 0;
	std::vector<EntityData> entities_;
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
*/
} // namespace ecs