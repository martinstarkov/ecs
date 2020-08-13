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

// TODO: Add a function called CreateComponent which will be called in AddComponent and generate a new ID if
// the AddComponent <T>::GetId() does not exist already, this will also resize the components vector and 
// grow them each by one

// TEMPORARY 
///*

#define DEBUG

#ifdef DEBUG
	#define EXPANDED( condition ) condition
	#define EXPAND(condition, ...) { internal::AssertCheck(condition, #condition, __FILE__, __LINE__, { __VA_ARGS__ }); }
// Note: add parentheses when condition contains non argument commas, e.g. when using metaprogramming structs
	#define assert(...) EXPANDED( EXPAND(__VA_ARGS__) )
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
	size_t total = 0;
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
	size_t max_size = std::max({ condition.first.size(), file.first.size(), line.first.size(), message.second.size() }) - 1;
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

inline void AssertCheck(bool assertion, const char* condition, const char* file, int line, std::initializer_list<std::variant<const char*, std::string>> msg) {
	if (!assertion) {
		ConsoleAssertion(std::cerr, " Assertion Failed ", { "Condition: ", condition }, { "File: ", file }, { "Line: ", std::to_string(line) }, { "Message: ", concatenate(msg) });
		abort();
	}
}

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
} // namespace internal

//*/ 
// END OF TEMPORARY

// Entity Component System
namespace ecs {

// Thanks to Piotr Skotnicki https://stackoverflow.com/a/25958302 for tuple_contains_type implementation
template <typename T, typename Tuple>
struct has_type;

template <typename T>
struct has_type<T, std::tuple<>> : std::false_type {};

template <typename T, typename U, typename... Ts>
struct has_type<T, std::tuple<U, Ts...>> : has_type<T, std::tuple<Ts...>> {};

template <typename T, typename... Ts>
struct has_type<T, std::tuple<T, Ts...>> : std::true_type {};

template <typename T, typename Tuple>
using tuple_contains_type = typename has_type<T, Tuple>::type;

template <typename T, typename ...Ts>
std::vector<T>& get(std::tuple<Ts...>& tuple) {
	// no testing as std::get prevents compilation unless type T exists in tuple
	return std::get<std::vector<T>&>(tuple);
}

// Thanks to Andy G https://gieseanw.wordpress.com/2017/05/03/a-true-heterogeneous-container-in-c/ for heterogeneous container concept
struct ComponentStorage {
public:
	ComponentStorage() = default;
	ComponentStorage(const ComponentStorage& _other) {
		*this = _other;
	}

	ComponentStorage& operator=(const ComponentStorage& _other) {
		clear();
		clear_functions = _other.clear_functions;
		copy_functions = _other.copy_functions;
		size_functions = _other.size_functions;
		for (auto&& copy_function : copy_functions) {
			copy_function(_other, *this);
		}
		return *this;
	}

	template<class T>
	void push_back(const T& _t) {
		// don't have it yet, so create functions for printing, copying, moving, and destroying
		if (components<T>.find(this) == std::end(components<T>)) {
			clear_functions.emplace_back([](ComponentStorage& _c) {components<T>.erase(&_c); });

			// if someone copies me, they need to call each copy_function and pass themself
			copy_functions.emplace_back([](const ComponentStorage& _from, ComponentStorage& _to) {
				components<T>[&_to] = components<T>[&_from];
			});
			size_functions.emplace_back([](const ComponentStorage& _c) {return components<T>[&_c].size(); });
		}
		auto& v = components<T>[this];
		v.push_back(_t);
	}

	void clear() {
		for (auto&& clear_func : clear_functions) {
			clear_func(*this);
		}
	}

	template<class T>
	size_t number_of() const {
		auto iter = components<T>.find(this);
		if (iter != components<T>.cend())
			return components<T>[this].size();
		return 0;
	}

	template<class T>
	std::vector<T>& getVector() const {
		assert(components<T>.find(this) != components<T>.cend(), "Could not find std::vector<", typeid(T).name(), "> in components");
		return components<T>[this];
	}

	template<class T>
	T& getVectorElement(size_t index) const {
		std::vector<T>& v = getVector<T>();
		assert(index < v.size(), "std::vector<", typeid(T).name(), ">[", std::to_string(index), "] is out of range");
		return v[index];
	}

	template<class ...Ts>
	std::tuple<std::vector<Ts>&...> getComponentVectors() const {
		return std::forward_as_tuple(getVector<Ts>()...);
	}

	template<class ...Ts>
	std::tuple<Ts&...> getComponents(size_t index) const {
		return std::forward_as_tuple(getVectorElement<Ts>(index)...);
	}

	size_t Size() const {
		// TODO: Change to a counter?
		return copy_functions.size();
	}

	size_t TotalSize() const {
		size_t sum = 0;
		for (auto&& size_func : size_functions) {
			sum += size_func(*this);
		}
		// gotta be careful about this overflowing
		return sum;
	}

	~ComponentStorage() {
		clear();
	}

private:
	template<class T>
	static std::unordered_map<const ComponentStorage*, std::vector<T>> components;

	std::vector<std::function<void(ComponentStorage&)>> clear_functions;
	std::vector<std::function<void(const ComponentStorage&, ComponentStorage&)>> copy_functions;
	std::vector<std::function<size_t(const ComponentStorage&)>> size_functions;
};

template<class T>
std::unordered_map<const ComponentStorage*, std::vector<T>> ComponentStorage::components;
/*

using EntityId = int32_t;
using ComponentId = int32_t;
using ComponentMask = std::vector<bool>;
constexpr size_t STARTING_ENTITY_COUNT = 100;

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
	size_t GetComponentCount() {
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
	Manager(size_t starting_entity_count = STARTING_ENTITY_COUNT) {
		//(GenerateComponentId(), ...);
		GrowByEntity(starting_entity_count);
	}
	Manager(Manager&& other) = default;
	Manager& operator=(Manager&& other) = default;
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
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