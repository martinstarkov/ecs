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
#include <map>
#include <sstream>
#include <cmath>

#include <atomic>

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

// Entity Component System
namespace ecs {

using ComponentId = int64_t;
using Atomic_ComponentId = std::atomic_int64_t;
using ComponentIndex = int64_t;
using EntityId = int64_t;

template <typename T, typename ...Ts>
std::vector<T>& AccessTuple(std::tuple<Ts...>& tuple) {
	// no testing as std::get prevents compilation unless type T exists in tuple
	return std::get<std::vector<T>&>(tuple);
}

// Thanks to Andy G https://gieseanw.wordpress.com/2017/05/03/a-true-heterogeneous-container-in-c/ for heterogeneous container concept

template <class T>
struct ComponentIdWrapper {
	ComponentId id = -1;
};

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
			ids_<T>.resize(ids_<T>.size() + 1);
			ids_<T>[storage_index_].id = count;
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
		return ids_<T>[storage_index_].id;
	}
	template <class T>
	T& GetComponent(std::size_t index) const {
		std::vector<T>& v = GetComponentVector<T>();
		assert(index < v.size());
		return v[index];
	}
	template <class T>
	T& GetComponent(std::vector<T>& component_vector, std::size_t index) const {
		assert(index < component_vector.size());
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
	static std::vector<ComponentIdWrapper<T>> ids_;
	static std::vector<ComponentId> component_counts_;
	std::vector<std::function<void(const std::size_t)>> clear_functions_;
};
// Static ComponentStorage variables
template <class T>
std::vector<std::vector<T>> ComponentStorage::storage_;
template <class T>
std::vector<ComponentIdWrapper<T>> ComponentStorage::ids_;
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
	const ComponentStorage& GetComponentStorage() const {
		return component_storage_;
	}
private:
	EntityId entity_count_ = 0;
	ComponentStorage component_storage_;
	std::vector<EntityData> entities_;
	static std::size_t manager_id;
};
std::size_t Manager::manager_id = 0;



struct EntityData2 {
	std::vector<std::pair<ComponentId, std::any>> components;
	bool alive;
};

class Manager2 {
public:
	void ResizeEntities(std::size_t additional_amount) {
		entities_.resize(entities_.capacity() + additional_amount);
	}
	EntityId CreateEntity() {
		if (entity_count_ >= static_cast<EntityId>(entities_.capacity())) {
			ResizeEntities(static_cast<EntityId>(entities_.capacity() + 2));
		}
		EntityId free_index(entity_count_++);
		EntityData2 new_entity;
		new_entity.alive = true;
		entities_[free_index] = std::move(new_entity);
		return free_index;
	}
	template <class T, typename ...TArgs>
	void AddComponent(EntityId index, TArgs&&... args) {
		entities_[index].components.emplace_back(typeid(T).hash_code(), T{ std::forward<TArgs>(args)... });
	}
	template <class T>
	T& GetComponent(EntityId index) {
		std::vector<std::pair<ComponentId, std::any>>& cs = entities_[index].components;
		ComponentId id = typeid(T).hash_code();
		for (std::pair<ComponentId, std::any>& pair : cs) {
			if (pair.first == id) {
				return std::any_cast<T&>(pair.second);
			}
		}
		return std::any_cast<T&>(cs[0].second);
	}
	EntityId EntityCount() const {
		return entity_count_;
	}
private:
	EntityId entity_count_ = 0;
	std::vector<EntityData2> entities_;
};




extern Atomic_ComponentId component_counter;

template <typename T>
ComponentId GetTypeId() {
	static ComponentId id = ++component_counter;
	return id;
}

// In a *.cpp file:
Atomic_ComponentId component_counter{ 0 };

template<typename T>
void destructor(void* ptr) {
	static_cast<T*>(ptr)->~T();
}

typedef void (*destructor_func)(void*);

struct ComponentData {
	ComponentData(ComponentId id = -1, destructor_func destructor = nullptr, void* address = nullptr, std::size_t bytes_from_begin = 0, std::size_t size = 0) : id(id), destructor(destructor), address(address), bytes_from_begin(bytes_from_begin), size(size) {}
	ComponentId id;
	destructor_func destructor;
	void* address;
	std::size_t bytes_from_begin;
	std::size_t size;
};

struct FreeComponent {
	FreeComponent(std::size_t bytes_from_begin, void* address, bool existed = true) : bytes_from_begin(bytes_from_begin), address(address), existed(existed) {}
	std::size_t bytes_from_begin;
	void* address;
	bool existed;
};

struct EntityData4 {
	std::vector<ComponentData> components;
	bool alive;
};

class Manager4 {
public:
	Manager4() = default;
	~Manager4() {
		for (auto& e : entities_) {
			for (auto& c : e.components) {
				c.destructor(c.address);
			}
		}
		LOG("Destructed all component data");
		free(mega_array_);
		mega_array_ = nullptr;
		LOG("Freed memory of mega array");
	}
	Manager4(const Manager4&) = delete;
	Manager4& operator=(const Manager4&) = delete;
	// TODO: Later implement move operators (and perhaps copy?)
	Manager4(Manager4&&) = delete;
	Manager4& operator=(Manager4&&) = delete;
	void ResizeEntities(std::size_t new_entities) {
		//capacity_ = allComps * bytes;
		/*std::size_t multiplier = 1;
		if (new_entities * multiplier >= capacity_) {
			if (mega_array_ == nullptr) {
				std::size_t new_capacity = new_entities * multiplier;
				mega_array_ = malloc(new_capacity);
				capacity_ = new_capacity;
			} else {
			}
		}*/
		if (mega_array_ == nullptr) {
			std::size_t new_capacity = new_entities;
			mega_array_ = malloc(new_capacity);
			capacity_ = new_capacity;
			entities_.resize(entities_.capacity() + new_entities);
		} else {
			ReAllocate();
		}
	}
	EntityId CreateEntity() {
		if (entity_count_ >= entities_.capacity()) {
			ResizeEntities(entities_.capacity() + 2);
		}
		EntityId free_id(FindFreeEntityId());
		assert(!HasEntity(free_id));
		EntityData4 new_entity;
		new_entity.alive = true;
		entities_[free_id] = std::move(new_entity);
		return free_id;
	}
	void DestroyEntity(EntityId index) {
		assert(HasEntity(index));
		free_entity_list_.push_back(index);
		EntityData4& data = entities_[index];
		for (auto& c : data.components) {
			auto it = free_component_map_.find(c.size);
			if (it == std::end(free_component_map_)) {
				std::vector<FreeComponent> vector;
				vector.emplace_back(c.bytes_from_begin, c.address);
				free_component_map_.emplace(c.size, std::move(vector));
			} else {
				it->second.emplace_back(c.bytes_from_begin, c.address);
			}
			c.destructor(c.address);
			memset(c.address, 0, c.size);
		}
		data.alive = false;
		data.components.resize(0);
		--entity_count_;
	}
	bool HasEntity(EntityId index) {
		if (index >= 0 && index < static_cast<EntityId>(entities_.size())) {
			return entities_[index].alive;
		}
		return false;
	}
	template <class T, typename ...TArgs>
	void AddComponent(EntityId index, TArgs&&... args) {
		auto it = GetComponentIterator<T>(index);
		auto& components = entities_[index].components;
		if (it == std::end(components)) {
			std::size_t size = sizeof(T);
			if (size_ + size >= capacity_) {
				ReAllocate();
			}
			FreeComponent free_component = FindFreeComponentAddress(size);
			new(free_component.address) T(std::forward<TArgs>(args)...);
			destructor_func destruct = &destructor<T>;
			components.emplace_back(GetTypeId<T>(), destruct, static_cast<void*>(free_component.address), free_component.bytes_from_begin, size);
			if (!free_component.existed) {
				size_ += size;
			}
		} else {
			//LOG("Replacing component");
			*static_cast<T*>((it->address)) = std::move(T(std::forward<TArgs>(args)...));
			// Cannot add more than one of a single type of component
		}
	}
	template <class T>
	void RemoveComponent(EntityId index) {
		// Make this assertion???
		auto it = GetComponentIterator<T>(index);
		auto& components = entities_[index].components;
		if (it != std::end(components)) {
			std::size_t size = sizeof(T);
			auto map_it = free_component_map_.find(size);
			if (map_it == std::end(free_component_map_)) {
				std::vector<FreeComponent> vector;
				vector.emplace_back(it->bytes_from_begin, it->address);
				free_component_map_.emplace(size, std::move(vector));
			} else {
				map_it->second.emplace_back(it->bytes_from_begin, it->address);
			}
			it->destructor(it->address);
			memset(it->address, 0, it->size);
			components.erase(it);
		}
	}
	template <class T>
	bool HasComponent(EntityId index) {
		auto it = GetComponentIterator<T>(index);
		if (it != std::end(entities_[index].components)) {
			return true;
		}
		return false;
	}
	template <class T>
	T* GetComponentP(EntityId index) {
		auto it = GetComponentIterator<T>(index);
		if (it != std::end(entities_[index].components)) {
			return static_cast<T*>(it->address);
		}
		return nullptr;
	}
	template <class T>
	T& GetComponent(EntityId index) {
		T* component = GetComponentP<T>(index);
		assert(component, "No matching component exists");
		return *component;
	}
	std::size_t EntityCount() const {
		return entity_count_;
	}
private:
	template <class T>
	auto GetComponentIterator(EntityId index) {
		assert(HasEntity(index), "Entity does not exist in manager");
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		auto end = std::end(components);
		for (auto it = std::begin(components); it != end; ++it) {
			if (it->id == id) {
				return it;
			}
		}
		return end;
	}
	void ReAllocate() {
		std::size_t new_capacity = capacity_ * 2;
		LOG("Reallocating " << new_capacity << " bytes...");
		void* p = realloc(mega_array_, new_capacity);
		assert(p, "could not reallocate ", std::to_string(new_capacity));
		mega_array_ = p;
		for (auto& e : entities_) {
			for (auto& c : e.components) {
				c.address = (void*)((char*)mega_array_ + c.bytes_from_begin);
			}
		}
		capacity_ = new_capacity;
	}
	std::size_t Size() const {
		return size_;
	}
	std::size_t Capacity() const {
		return capacity_;
	}
	FreeComponent FindFreeComponentAddress(std::size_t required_size) {
		if (free_component_map_.size() > 0) {
			auto it = free_component_map_.find(required_size);
			if (it != std::end(free_component_map_)) {
				std::vector<FreeComponent>& free_components = it->second;
				if (free_components.size() > 0) {
					// get copy of first free component
					FreeComponent free_component = *free_components.begin();
					if (free_components.size() == 1) { // one free component, erase size entry
						free_component_map_.erase(it);
					} else { // multiple free_components, swap and pop
						std::iter_swap(std::begin(free_components), std::end(free_components) - 1);
						free_components.pop_back();
					}
					return free_component;
				}
			}
		}
		return FreeComponent(size_, (void*)((char*)mega_array_ + size_), false);
	}
	std::size_t FindFreeEntityId() {
		if (free_entity_list_.size() > 0) {
			std::size_t free_id = free_entity_list_.front();
			if (free_entity_list_.size() == 1) { // one free entity, clear vector
				free_entity_list_.resize(0);
			} else { // multiple free entities, swap and pop
				std::iter_swap(std::begin(free_entity_list_), std::end(free_entity_list_) - 1);
				free_entity_list_.pop_back();
			}
			return free_id;
		}
		return entity_count_++;
	}
public:
	std::unordered_map<std::size_t, std::vector<FreeComponent>> free_component_map_;
	std::vector<std::size_t> free_entity_list_;
private:
	std::size_t entity_count_{ 0 };
	std::size_t capacity_{ 0 };
	std::size_t size_{ 0 };
	void* mega_array_ = nullptr;
	std::vector<EntityData4> entities_;
};







template<typename T>
void DestroyComponent(char* ptr) {
	static_cast<T*>(static_cast<void*>(ptr))->~T();
}

typedef void (*destructor_function)(char*);

struct EntityData5 {
	std::vector<std::pair<ComponentId, std::int64_t>> components;
	bool alive;
};

class Manager5 {
private:
	auto GetDestructorIterator(ComponentId id) {
		auto end = std::end(destructors_);
		for (auto it = std::begin(destructors_); it != end; ++it) {
			if (id == it->first) {
				return it;
			}
		}
		return end;
	}
public:
	Manager5() = default;
	~Manager5() {
		for (auto& entity : entities_) {
			for (auto& pair : entity.components) {
				auto destructor_it = GetDestructorIterator(pair.first);
				assert(destructor_it != std::end(destructors_), "Could not find destructor iterator");
				destructor_it->second(mega_array_ + pair.second);
			}
		}
		LOG("Destructed all component data");
		free(mega_array_);
		mega_array_ = nullptr;
		LOG("Freed memory of mega array");
	}
	Manager5(const Manager5&) = delete;
	Manager5& operator=(const Manager5&) = delete;
	// TODO: Later implement move operators (and perhaps copy?)
	Manager5(Manager5&&) = delete;
	Manager5& operator=(Manager5&&) = delete;
	void ResizeEntities(std::size_t new_entities) {
		if (!mega_array_) {
			std::size_t new_capacity = new_entities;
			//LOG("Allocating " << new_capacity << " bytes...");
			mega_array_ = static_cast<char*>(malloc(new_capacity));
			capacity_ = new_capacity;
			entities_.resize(entities_.capacity() + new_entities);
		} else {
			ReAllocate();
		}
	}
	EntityId CreateEntity() {
		if (entity_count_ >= entities_.capacity()) {
			ResizeEntities(entities_.capacity() + 2);
		}
		EntityId free_id(FindFreeEntityId());
		assert(!HasEntity(free_id));
		EntityData5 new_entity;
		new_entity.alive = true;
		entities_[free_id] = std::move(new_entity);
		return free_id;
	}
	/*void DestroyEntity(EntityId index) {
		assert(HasEntity(index));
		free_entity_list_.push_back(index);
		EntityData4& data = entities_[index];
		for (auto& c : data.components) {
			auto it = free_component_map_.find(c.size);
			if (it == std::end(free_component_map_)) {
				std::vector<FreeComponent> vector;
				vector.emplace_back(c.bytes_from_begin, c.address);
				free_component_map_.emplace(c.size, std::move(vector));
			} else {
				it->second.emplace_back(c.bytes_from_begin, c.address);
			}
			c.destructor(c.address);
			memset(c.address, 0, c.size);
		}
		data.alive = false;
		data.components.resize(0);
		--entity_count_;
	}*/
	bool HasEntity(EntityId index) {
		if (index >= 0 && index < static_cast<EntityId>(entities_.size())) {
			return entities_[index].alive;
		}
		return false;
	}
	template <class T, typename ...TArgs>
	void AddComponent(EntityId index, TArgs&&... args) {
		assert(HasEntity(index), "No matching entity");
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		for (auto& pair : components) {
			if (pair.first == id) {
				*static_cast<T*>(static_cast<void*>(mega_array_ + pair.second)) = std::move(T(std::forward<TArgs>(args)...));
				return;
			}
		}
		std::size_t size = sizeof(T);
		if (size_ + size >= capacity_) {
			ReAllocate();
		}
		auto pair = GetFreeComponentAddress(size);
		new((void*)(mega_array_ + pair.first)) T(std::forward<TArgs>(args)...);
		components.emplace_back(id, pair.first);
		// Component did not exist in free_component_map_
		if (!pair.second) {
			if (GetDestructorIterator(id) == std::end(destructors_)) {
				destructors_.emplace_back(id, &DestroyComponent<T>);
			}
			size_ += size;
		}
	}
	template <class T>
	void RemoveComponent(EntityId index) {
		assert(HasEntity(index));
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		for (auto it = std::begin(components); it != std::end(components); ++it) {
			if (it->first == id) {
				auto address = mega_array_ + it->second;
				std::size_t size = sizeof(T);
				auto map_it = free_component_map_.find(size);
				if (map_it == std::end(free_component_map_)) {
					std::vector<char*> free_addresses;
					free_addresses.emplace_back(address);
					free_component_map_.emplace(size, std::move(free_addresses));
				} else {
					map_it->second.emplace_back(address);
				}
				auto mem_address = static_cast<void*>(address);
				static_cast<T*>(mem_address)->~T();
				memset(mem_address, 0, size);
				components.erase(it);
				return;
			}
		}
	}
	template <class T>
	bool HasComponent(EntityId index) {
		assert(HasEntity(index), "No matching entity");
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		for (auto& pair : components) {
			if (pair.first == id) {
				return true;
			}
		}
		return false;
	}
	template <class T>
	T* GetComponentP(EntityId index) {
		assert(HasEntity(index), "No matching entity");
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		for (auto& pair : components) {
			if (pair.first == id) {
				return static_cast<T*>(static_cast<void*>(mega_array_ + pair.second));
			}
		}
		return nullptr;
	}
	template <class T>
	T& GetComponent(EntityId index) {
		T* component = GetComponentP<T>(index);
		assert(component, "No matching component");
		return *component;
	}
	std::size_t EntityCount() const {
		return entity_count_;
	}
private:
	/*template <class T>
	auto GetComponentIterator(EntityId index) {
		assert(HasEntity(index), "Entity does not exist in manager");
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		auto end = std::end(components);
		for (auto it = std::begin(components); it != end; ++it) {
			if (it->id == id) {
				return it;
			}
		}
		return end;
	}*/
	void ReAllocate() {
		std::size_t new_capacity = capacity_ * 2;
		//LOG("Reallocating " << new_capacity << " bytes...");
		void* p = realloc(mega_array_, new_capacity);
		assert(p, "could not reallocate ", std::to_string(new_capacity));
		mega_array_ = static_cast<char*>(p);
		capacity_ = new_capacity;
	}
	template <typename T>
	auto GetComponentIterator(EntityId index) {
		return GetComponentIterator(index, GetTypeId<T>());
	}
	auto GetComponentIterator(EntityId index, ComponentId id) {
		assert(HasEntity(index));
		auto& components = entities_[index].components;
		auto end = std::end(components);
		for (auto it = std::begin(components); it != end; ++it) {
			if (id == it->first) {
				return it;
			}
		}
		return end;
	}
	template <typename T>
	void* GetComponentAddress(EntityId index) {
		auto it = GetComponentIterator(index, GetTypeId<T>());
		if (it != std::end(entities_[index].components)) {
			return static_cast<void*>(mega_array_ + it->second);
		}
		return nullptr;
	}
	std::size_t Size() const {
		return size_;
	}
	std::size_t Capacity() const {
		return capacity_;
	}
	std::pair<std::int64_t, bool> GetFreeComponentAddress(std::size_t required_size) {
		if (free_component_map_.size() > 0) {
			auto it = free_component_map_.find(required_size);
			if (it != std::end(free_component_map_)) {
				auto& free_addresses = it->second;
				if (free_addresses.size() > 0) {
					// get copy of first free component
					auto address_offset = free_addresses.front();
					if (free_addresses.size() == 1) { // one free component, erase size entry
						free_component_map_.erase(it);
					} else { // multiple free_components, swap and pop
						std::iter_swap(std::begin(free_addresses), std::end(free_addresses) - 1);
						free_addresses.pop_back();
					}
					return { address_offset, true };
				}
			}
		}
		return { size_, false };
	}
	std::size_t FindFreeEntityId() {
		if (free_entity_list_.size() > 0) {
			std::size_t free_id = free_entity_list_.front();
			if (free_entity_list_.size() == 1) { // one free entity, clear vector
				free_entity_list_.resize(0);
			} else { // multiple free entities, swap and pop
				std::iter_swap(std::begin(free_entity_list_), std::end(free_entity_list_) - 1);
				free_entity_list_.pop_back();
			}
			return free_id;
		}
		return entity_count_++;
	}
public:
	// CONSIDER: Performance improvement if switching to map or vector of pairs?
	std::unordered_map<std::size_t, std::vector<std::int64_t>> free_component_map_;
	std::vector<std::size_t> free_entity_list_;
private:
	std::vector<std::pair<ComponentId, destructor_function>> destructors_;
	std::size_t entity_count_{ 0 };
	std::size_t capacity_{ 0 };
	std::size_t size_{ 0 };
	char* mega_array_ = nullptr;
	std::vector<EntityData5> entities_;
};




















class BaseComponent {};

template <class T>
class Component : public BaseComponent {
public:
	Component(T data) : data(std::move(data)) {}
	~Component() {
		data.~T();
	}
	T& get() { return data; }
private:
	T data;
};

struct EntityData3 {
	std::vector<std::pair<ComponentId, BaseComponent*>> components;
	bool alive;
};

class Manager3 {
public:
	void ResizeEntities(std::size_t additional_amount) {
		entities_.resize(entities_.capacity() + additional_amount);
	}
	EntityId CreateEntity() {
		if (entity_count_ >= entities_.capacity()) {
			ResizeEntities(entities_.capacity() + 2);
		}
		EntityId free_index(entity_count_++);
		EntityData3 new_entity;
		new_entity.alive = true;
		entities_[free_index] = std::move(new_entity);
		return free_index;
	}
	bool HasEntity(EntityId index) {
		if (index >= 0 && index < static_cast<EntityId>(entities_.size())) {
			return entities_[index].alive;
		}
		return false;
	}
	template <class T, typename ...TArgs>
	void AddComponent(EntityId index, TArgs&&... args) {
		auto it = GetComponentIterator<T>(index);
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		if (it == std::end(components)) {
			components.emplace_back(id, new Component<T>(T(std::forward<TArgs>(args)...)));
		} else {
			BaseComponent* ptr = it->second;
			static_cast<Component<T>*>(ptr)->get().~T();
			delete ptr;
			it->second = new Component<T>(T(std::forward<TArgs>(args)...));
		}
	}
	template <class T>
	void RemoveComponent(EntityId index) {
		auto it = GetComponentIterator<T>(index);
		auto& components = entities_[index].components;
		if (it != std::end(components)) {
			BaseComponent* ptr = it->second;
			static_cast<Component<T>*>(ptr)->get().~T();
			delete ptr;
			ptr = nullptr;
			components.erase(it);
		}
	}
	template <class T>
	bool HasComponent(EntityId index) {
		auto it = GetComponentIterator<T>(index);
		return it->second;
	}
	template <class T>
	T* GetComponentP(EntityId index) {
		auto it = GetComponentIterator<T>(index);
		if (it->second) {
			return &static_cast<Component<T>*>(it->second)->get();
		}
		return nullptr;
	}
	template <class T>
	T& GetComponent(EntityId index) {
		auto it = GetComponentIterator<T>(index);
		assert(it->second);
		return static_cast<Component<T>*>(it->second)->get();
	}
	std::size_t EntityCount() const {
		return entity_count_;
	}
private:
	template <class T>
	auto GetComponentIterator(EntityId index) {
		assert(HasEntity(index), "Entity does not exist in manager");
		auto& components = entities_[index].components;
		ComponentId id = GetTypeId<T>();
		auto end = std::end(components);
		for (auto it = std::begin(components); it != end; ++it) {
			if (it->first == id) {
				return it;
			}
		}
		return end;
	}
	std::size_t entity_count_ = 0;
	std::vector<EntityData3> entities_;
};












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