/*

MIT License

Copyright (c) 2024 | Martin Starkov | https://github.com/martinstarkov

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

#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {

class Entity;
class Manager;

enum class LoopCriterion {
	None,
	WithComponents,
	WithoutComponents
};

template <LoopCriterion C, typename... Ts>
class EntityContainer;

using Entities = EntityContainer<LoopCriterion::None>;

template <typename... TComponents>
using EntitiesWith = EntityContainer<LoopCriterion::WithComponents, TComponents...>;

template <typename... TComponents>
using EntitiesWithout = EntityContainer<LoopCriterion::WithoutComponents, TComponents...>;

template <LoopCriterion C, typename TC, typename... Ts>
class EntityContainerIterator;

using Index	  = std::uint32_t;
using Version = std::uint32_t;

inline constexpr const Version null_version{ 0 };

namespace tt {

template <typename Struct, typename = void, typename... Ts>
struct is_direct_list_initializable_impl : std::false_type {};

template <typename Struct, typename... Ts>
struct is_direct_list_initializable_impl<
	Struct, std::void_t<decltype(Struct{ std::declval<Ts>()... })>, Ts...> : std::true_type {};

template <typename Struct, typename... Ts>
using is_direct_list_initializable = is_direct_list_initializable_impl<Struct, void, Ts...>;

template <typename Struct, typename... Ts>
constexpr bool is_direct_list_initializable_v = is_direct_list_initializable<Struct, Ts...>::value;

template <typename Struct, typename... Ts>
using is_aggregate_initializable = std::conjunction<
	std::is_aggregate<Struct>, is_direct_list_initializable<Struct, Ts...>,
	std::negation<std::conjunction<
		std::bool_constant<sizeof...(Ts) == 1>,
		std::is_same<std::decay_t<std::tuple_element_t<0, std::tuple<Ts...>>>, Struct>>>>;

template <typename S, typename... Ts>
struct aggregate_initializable {
	constexpr static const bool value{ is_aggregate_initializable<S, Ts...>::value };
};

template <typename S>
struct aggregate_initializable<S> : std::false_type {};

template <typename Struct, typename... Ts>
constexpr bool is_aggregate_initializable_v = aggregate_initializable<Struct, Ts...>::value;

} // namespace tt

namespace impl {

class AbstractPool {
public:
	virtual ~AbstractPool()											  = default;
	[[nodiscard]] virtual std::shared_ptr<AbstractPool> Clone() const = 0;
	virtual void Copy(Index from_entity, Index to_entity)			  = 0;
	// Maintains size of pool as opposed to Reset().
	virtual void Clear()							   = 0;
	virtual void Reset()							   = 0;
	virtual bool Remove(Index entity)				   = 0;
	[[nodiscard]] virtual bool Has(Index entity) const = 0;
};

template <typename T>
class Pool : public AbstractPool {
	static_assert(
		std::is_move_constructible_v<T>,
		"Cannot create pool for component which is not move constructible"
	);
	static_assert(
		std::is_destructible_v<T>, "Cannot create pool for component which is not destructible"
	);

public:
	Pool() = default;

	~Pool()						 = default;
	Pool(Pool&&)				 = default;
	Pool& operator=(Pool&&)		 = default;
	Pool(const Pool&)			 = default;
	Pool& operator=(const Pool&) = default;

	[[nodiscard]] virtual std::shared_ptr<AbstractPool> Clone() const final {
		// The reason this is not statically asserted is because it would disallow move-only
		// component pools.
		if constexpr (std::is_copy_constructible_v<T>) {
			auto pool		  = std::make_shared<Pool<T>>();
			pool->components_ = components_;
			pool->dense_	  = dense_;
			pool->sparse_	  = sparse_;
			return pool;
		} else {
			assert(!"Cannot clone component pool with a non copy constructible component");
			return nullptr;
		}
	}

	virtual void Copy(Index from_entity, Index to_entity) final {
		// Same reason as given in Clone() for why no static_assert.
		if constexpr (std::is_copy_constructible_v<T>) {
			assert(Has(from_entity));
			if (!Has(to_entity)) {
				Add(to_entity, components_[sparse_[from_entity]]);
			} else {
				components_.emplace(
					components_.begin() + sparse_[to_entity], components_[sparse_[from_entity]]
				);
			}
		} else {
			assert(!"Cannot copy an entity with a non copy constructible component");
		}
	}

	virtual void Clear() final {
		components_.clear();
		dense_.clear();
		sparse_.clear();
	}

	virtual void Reset() final {
		Clear();

		components_.shrink_to_fit();
		dense_.shrink_to_fit();
		sparse_.shrink_to_fit();
	}

	virtual bool Remove(Index entity) final {
		if (Has(entity)) {
			// See https://skypjack.github.io/2020-08-02-ecs-baf-part-9/ for
			// in-depth explanation. In short, swap with back and pop back,
			// relinking sparse ids after.
			const Index last{ dense_.back() };
			std::swap(dense_.back(), dense_[sparse_[entity]]);
			std::swap(components_.back(), components_[sparse_[entity]]);
			assert(last < sparse_.size());
			std::swap(sparse_[last], sparse_[entity]);
			dense_.pop_back();
			components_.pop_back();
			return true;
		}
		return false;
	}

	[[nodiscard]] virtual bool Has(Index entity) const final {
		if (entity >= sparse_.size()) {
			return false;
		}
		auto s = sparse_[entity];
		if (s >= dense_.size()) {
			return false;
		}
		return entity == dense_[s];
	}

	[[nodiscard]] const T& Get(Index entity) const {
		assert(Has(entity));
		assert(
			sparse_[entity] < components_.size() &&
			"Likely attempting to retrieve a component before it has been fully added to the "
			"entity, e.g. self-referencing Get() in the Add() constructor call"
		);
		return components_[sparse_[entity]];
	}

	[[nodiscard]] T& Get(Index entity) {
		return const_cast<T&>(static_cast<const Pool<T>&>(*this).Get(entity));
	}

	template <typename... Ts>
	T& Add(Index entity, Ts&&... constructor_args) {
		static_assert(
			std::is_constructible_v<T, Ts...> || tt::is_aggregate_initializable_v<T, Ts...>,
			"Cannot add component which is not constructible from given arguments"
		);
		if (entity < sparse_.size()) {				 // Entity has had the component before.
			if (sparse_[entity] < dense_.size() &&
				dense_[sparse_[entity]] == entity) { // Entity currently has the component.
				// Replace the current component with a new component.
				T& component{ components_[sparse_[entity]] };
				// This approach prevents the creation of a temporary component object.
				component.~T();
				if constexpr (std::is_aggregate_v<T>) {
					new (&component) T{ std::forward<Ts>(constructor_args)... };
				} else {
					new (&component) T(std::forward<Ts>(constructor_args)...);
				}
				return component;
			}
			// Entity currently does not have the component.
			sparse_[entity] = static_cast<Index>(dense_.size());
		} else {
			// Entity has never had the component.
			sparse_.resize(static_cast<std::size_t>(entity) + 1, static_cast<Index>(dense_.size()));
		}
		// Add new component to the entity.
		dense_.push_back(entity);
		if constexpr (std::is_aggregate_v<T>) {
			return components_.emplace_back(std::move(T{ std::forward<Ts>(constructor_args)... }));
		} else {
			return components_.emplace_back(std::forward<Ts>(constructor_args)...);
		}
	}

private:
	std::vector<T> components_;
	std::vector<Index> dense_;
	std::vector<Index> sparse_;
};

// Modified version of:
// https://github.com/syoyo/dynamic_bitset/blob/master/dynamic_bitset.hh

class DynamicBitset {
public:
	DynamicBitset()								   = default;
	~DynamicBitset()							   = default;
	DynamicBitset(DynamicBitset&&)				   = default;
	DynamicBitset& operator=(DynamicBitset&&)	   = default;
	DynamicBitset(const DynamicBitset&)			   = default;
	DynamicBitset& operator=(const DynamicBitset&) = default;

	void Set(std::size_t index, bool value = true) {
		std::size_t byte_index{ index / 8 };
		std::uint8_t offset{ static_cast<std::uint8_t>(index % 8) };
		std::uint8_t bitfield{ static_cast<std::uint8_t>(1 << offset) };

		assert(byte_index < data_.size());

		if (value) {
			data_[byte_index] |= bitfield;
		} else {
			data_[byte_index] &= (~bitfield);
		}
	}

	[[nodiscard]] bool operator[](std::size_t index) const {
		std::size_t byte_index{ index / 8 };
		std::size_t offset{ index % 8 };

		assert(byte_index < data_.size());
		int set{ (data_[byte_index] >> offset) & 0x1 };
		return set;
	}

	bool operator==(const DynamicBitset& other) const {
		return data_ == other.data_;
	}

	[[nodiscard]] std::size_t Size() const {
		return bit_count_;
	}

	[[nodiscard]] std::size_t Capacity() const {
		return data_.capacity();
	}

	void Reserve(std::size_t new_capacity) {
		std::size_t byte_count{ GetByteCount(new_capacity) };
		data_.reserve(byte_count);
	}

	void Resize(std::size_t new_size, bool value) {
		std::size_t byte_count{ GetByteCount(new_size) };
		bit_count_ = new_size;
		data_.resize(byte_count, value);
	}

	void Clear() {
		bit_count_ = 0;
		data_.clear();
	}

	void ShrinkToFit() {
		data_.shrink_to_fit();
	}

private:
	[[nodiscard]] std::size_t GetByteCount(std::size_t bit_count) {
		std::size_t byte_count{ 1 };
		if (bit_count >= 8) {
			assert(1 + (bit_count - 1) / 8 > 0);
			byte_count = 1 + (bit_count - 1) / 8;
		}
		return byte_count;
	}

	std::size_t bit_count_{ 0 };
	std::vector<std::uint8_t> data_;
};

struct ManagerInstance {
	Index next_entity_{ 0 };
	Index count_{ 0 };
	bool refresh_required_{ false };
	DynamicBitset entities_;
	DynamicBitset refresh_;
	std::vector<Version> versions_;
	std::vector<std::shared_ptr<AbstractPool>> pools_;
	std::deque<Index> free_entities_;
};

} // namespace impl

class Manager {
public:
	Manager() {
		instance_ = std::make_shared<impl::ManagerInstance>();
		// Reserve capacity for 1 entity so that manager size will double in powers
		// of 2.
		Reserve(1);
	}

	~Manager()						   = default;
	Manager(Manager&&)				   = default;
	Manager& operator=(Manager&&)	   = default;
	Manager(const Manager&)			   = default;
	Manager& operator=(const Manager&) = default;

	bool operator==(const Manager& other) const {
		return instance_ == other.instance_;
	}

	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}

	[[nodiscard]] Manager Clone() const {
		Manager clone;
		clone.instance_->count_			   = instance_->count_;
		clone.instance_->next_entity_	   = instance_->next_entity_;
		clone.instance_->entities_		   = instance_->entities_;
		clone.instance_->refresh_		   = instance_->refresh_;
		clone.instance_->refresh_required_ = instance_->refresh_required_;
		clone.instance_->versions_		   = instance_->versions_;
		clone.instance_->free_entities_	   = instance_->free_entities_;
		clone.instance_->pools_.resize(instance_->pools_.size(), nullptr);
		for (std::size_t i{ 0 }; i < instance_->pools_.size(); ++i) {
			auto& pool{ instance_->pools_[i] };
			if (pool == nullptr) {
				continue;
			}
			pool = pool->Clone();
			assert(pool != nullptr && "Cloning manager failed");
		}
		return clone;
	}

	void Refresh() {
		if (instance_->refresh_required_) {
			// This must be set before refresh starts in case
			// events are called (for instance during entity deletion).
			instance_->refresh_required_ = false;
			assert(
				instance_->entities_.Size() == instance_->versions_.size() &&
				"Refresh failed due to varying entity vector and version vector "
				"size"
			);
			assert(
				instance_->entities_.Size() == instance_->refresh_.Size() &&
				"Refresh failed due to varying entity vector and refresh vector "
				"size"
			);
			assert(
				instance_->next_entity_ <= instance_->entities_.Size() &&
				"Next available entity must not be out of bounds of entity vector"
			);
			Index alive{ 0 };
			Index dead{ 0 };
			for (Index entity{ 0 }; entity < instance_->next_entity_; ++entity) {
				// Entity was marked for refresh.
				if (instance_->refresh_[entity]) {
					instance_->refresh_.Set(entity, false);
					if (instance_->entities_[entity]) { // Marked for deletion.
						ClearEntity(entity);
						instance_->entities_.Set(entity, false);
						++instance_->versions_[entity];
						instance_->free_entities_.emplace_back(entity);
						++dead;
					} else { // Marked for 'creation'.
						instance_->entities_.Set(entity, true);
						++alive;
					}
				}
			}
			// Update entity count with net change.
			instance_->count_ += alive;
			instance_->count_  = instance_->count_ > dead ? instance_->count_ - dead : 0;
		}
	}

	void Reserve(std::size_t capacity) {
		instance_->entities_.Reserve(capacity);
		instance_->refresh_.Reserve(capacity);
		instance_->versions_.reserve(capacity);
		assert(
			instance_->entities_.Capacity() == instance_->refresh_.Capacity() &&
			"Entity and refresh vectors must have the same capacity"
		);
	}

	Entity CreateEntity();

	template <typename... Ts>
	void CopyEntity(const Entity& from, Entity& to);

	template <typename... Ts>
	Entity CopyEntity(const Entity& from);

	// TODO: Add const versions of the three functions below.

	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWith<Ts...> EntitiesWith();

	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWithout<Ts...> EntitiesWithout();

	[[nodiscard]] ecs::Entities Entities();

	[[nodiscard]] std::size_t Size() const {
		return instance_->count_;
	}

	[[nodiscard]] std::size_t Capacity() const {
		return instance_->versions_.capacity();
	}

	void Clear() {
		instance_->count_			 = 0;
		instance_->next_entity_		 = 0;
		instance_->refresh_required_ = false;

		instance_->entities_.Clear();
		instance_->refresh_.Clear();
		instance_->versions_.clear();
		instance_->free_entities_.clear();

		for (auto& pool : instance_->pools_) {
			if (pool != nullptr) {
				pool->Clear();
			}
		}
	}

	void Reset() {
		Clear();

		instance_->entities_.ShrinkToFit();
		instance_->refresh_.ShrinkToFit();
		instance_->versions_.shrink_to_fit();
		instance_->free_entities_.shrink_to_fit();

		instance_->pools_.clear();
		instance_->pools_.shrink_to_fit();

		Reserve(1);
	}

private:
	friend struct std::hash<Entity>;
	friend class Entity;

	Manager(int) {}

	template <LoopCriterion C, typename... Ts>
	friend class EntityContainer;
	friend class Entity;

	template <typename... Ts>
	void CopyEntity(Index from_id, Version from_version, Index to_id, Version to_version) {
		using namespace impl;
		assert(
			IsAlive(from_id, from_version) &&
			"Cannot copy from entity which has not been initialized from the "
			"manager"
		);
		assert(
			IsAlive(to_id, to_version) &&
			"Cannot copy to entity which has not been initialized from the "
			"manager"
		);
		if constexpr (sizeof...(Ts) > 0) { // Copy only specific components.
			static_assert(
				std::conjunction_v<std::is_copy_constructible<Ts>...>,
				"Cannot copy entity with a component that is not copy constructible"
			);
			auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
			bool manager_has{ ((std::get<Pool<Ts>*>(pools) != nullptr) && ...) };
			assert(
				manager_has && "Cannot copy entity with a component that is not "
							   "even in the manager"
			);
			bool entity_has{ (std::get<Pool<Ts>*>(pools)->template Pool<Ts>::Has(from_id) && ...) };
			assert(entity_has && "Cannot copy entity with a component that it does not have");
			(std::get<Pool<Ts>*>(pools)->template Pool<Ts>::Copy(from_id, to_id), ...);
		} else { // Copy all components.
			for (auto& pool : instance_->pools_) {
				if (pool != nullptr && pool->Has(from_id)) {
					pool->Copy(from_id, to_id);
				}
			}
		}
	}

	void GenerateEntity(Index& entity, Version& version) {
		entity = 0;
		// Pick entity from free list before trying to increment entity counter.
		if (instance_->free_entities_.size() > 0) {
			entity = instance_->free_entities_.front();
			instance_->free_entities_.pop_front();
		} else {
			entity = instance_->next_entity_++;
		}
		// Double the size of the manager if capacity is reached.
		if (entity >= instance_->entities_.Size()) {
			Resize(instance_->versions_.capacity() * 2);
		}
		assert(
			entity < instance_->entities_.Size() &&
			"Created entity is outside of manager entity vector range"
		);
		assert(!instance_->entities_[entity] && "Cannot create new entity from live entity");
		assert(
			!instance_->refresh_[entity] && "Cannot create new entity from refresh marked entity"
		);
		// Mark entity for refresh.
		instance_->refresh_.Set(entity, true);
		instance_->refresh_required_ = true;
		// Entity version incremented here.
		version = ++instance_->versions_[entity];
	}

	void Resize(std::size_t size) {
		if (size > instance_->entities_.Size()) {
			instance_->entities_.Resize(size, false);
			instance_->refresh_.Resize(size, false);
			instance_->versions_.resize(size, null_version);
		}
		assert(
			instance_->entities_.Size() == instance_->versions_.size() &&
			"Resize failed due to varying entity vector and version vector size"
		);
		assert(
			instance_->entities_.Size() == instance_->refresh_.Size() &&
			"Resize failed due to varying entity vector and refresh vector size"
		);
	}

	void ClearEntity(Index entity) {
		for (auto& pool : instance_->pools_) {
			if (pool != nullptr) {
				pool->Remove(entity);
			}
		}
	}

	[[nodiscard]] bool IsAlive(Index entity, Version version) const {
		return version != null_version && entity < instance_->versions_.size() &&
			   instance_->versions_[entity] == version && entity < instance_->entities_.Size() &&
			   // Entity considered currently alive or entity marked
			   // for creation/deletion but not yet created/deleted.
			   (instance_->entities_[entity] || instance_->refresh_[entity]);
	}

	[[nodiscard]] bool IsActivated(Index entity) const {
		return entity < instance_->entities_.Size() && instance_->entities_[entity];
	}

	[[nodiscard]] Version GetVersion(Index entity) const {
		assert(entity < instance_->versions_.size());
		return instance_->versions_[entity];
	}

	[[nodiscard]] bool Match(Index entity1, Index entity2) const {
		for (auto& pool : instance_->pools_) {
			if (pool != nullptr) {
				bool has1{ pool->Has(entity1) };
				bool has2{ pool->Has(entity2) };
				// Check that one entity has a component while the other doesn't.
				if ((has1 || has2) && (!has1 || !has2)) {
					// Exit early if one non-matching component is found.
					return false;
				}
			}
		}
		return true;
	}

	void DestroyEntity(Index entity, Version version) {
		assert(entity < instance_->versions_.size());
		assert(entity < instance_->refresh_.Size());
		if (instance_->versions_[entity] == version) {
			if (instance_->entities_[entity]) {
				instance_->refresh_.Set(entity, true);
				instance_->refresh_required_ = true;
			} else if (instance_->refresh_[entity]) {
				/*
				 * Edge case where entity is created and marked for deletion
				 * before a Refresh() has been called.
				 * In this case, destroy and invalidate the entity without
				 * a Refresh() call. This is equivalent to an entity which
				 * never 'officially' existed in the manager.
				 */
				ClearEntity(entity);
				instance_->refresh_.Set(entity, false);
				++instance_->versions_[entity];
				instance_->free_entities_.emplace_back(entity);
			}
		}
	}

	template <typename T>
	[[nodiscard]] const impl::Pool<T>* GetPool(Index component) const {
		assert(component == GetId<T>() && "GetPool mismatch with component id");
		if (component < instance_->pools_.size()) {
			const auto& pool = instance_->pools_[component];
			// This is nullptr if the pool does not exist in the manager.
			return static_cast<impl::Pool<T>*>(pool.get());
		}
		return nullptr;
	}

	template <typename T>
	[[nodiscard]] impl::Pool<T>* GetPool(Index component) {
		assert(component == GetId<T>() && "GetPool mismatch with component id");
		if (component < instance_->pools_.size()) {
			auto& pool = instance_->pools_[component];
			// This is nullptr if the pool does not exist in the manager.
			return static_cast<impl::Pool<T>*>(pool.get());
		}
		return nullptr;
	}

	template <typename T>
	void Remove(Index entity, Index component) {
		auto pool{ GetPool<T>(component) };
		if (pool != nullptr) {
			pool->template Pool<T>::Remove(entity);
		}
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get(Index entity) const {
		using namespace impl;
		if constexpr (sizeof...(Ts) == 1) {
			const auto pool{ GetPool<Ts...>(GetId<Ts...>()) };
			assert(pool != nullptr && "Manager does not have the requested component");
			return pool->Get(entity);
		} else {
			const auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
			assert(
				((std::get<Pool<Ts>*>(pools) != nullptr) && ...) &&
				"Manager does not have at least one of the requested components"
			);
			return std::forward_as_tuple<const Ts&...>(
				(std::get<Pool<Ts>*>(pools)->template Pool<Ts>::Get(entity))...
			);
		}
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get(Index entity) {
		using namespace impl;
		if constexpr (sizeof...(Ts) == 1) {
			auto pool{ GetPool<Ts...>(GetId<Ts...>()) };
			assert(pool != nullptr && "Manager does not have the requested component");
			return pool->Get(entity);
		} else {
			auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
			assert(
				((std::get<Pool<Ts>*>(pools) != nullptr) && ...) &&
				"Manager does not have at least one of the requested components"
			);
			return std::forward_as_tuple<Ts&...>(
				(std::get<Pool<Ts>*>(pools)->template Pool<Ts>::Get(entity))...
			);
		}
	}

	template <typename T>
	[[nodiscard]] bool Has(Index entity, Index component) const {
		const auto pool{ GetPool<T>(component) };
		return pool != nullptr && pool->Has(entity);
	}

	template <typename T, typename... Ts>
	T& Add(Index entity, Index component, Ts&&... constructor_args) {
		if (component >= instance_->pools_.size()) {
			instance_->pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
		}
		impl::Pool<T>* pool{ GetPool<T>(component) };
		if (pool == nullptr) {
			std::shared_ptr<impl::Pool<T>> new_pool = std::make_shared<impl::Pool<T>>();
			assert(component < instance_->pools_.size() && "Component index out of range");
			instance_->pools_[component] = new_pool;
			pool						 = new_pool.get();
		}
		assert(pool != nullptr && "Could not create new component pool correctly");
		return pool->Add(entity, std::forward<Ts>(constructor_args)...);
	}

	template <typename T>
	[[nodiscard]] static Index GetId() {
		// Get the next available id save that id as static variable for the
		// component type.
		static Index id{ ComponentCount()++ };
		return id;
	}

	[[nodiscard]] static Index& ComponentCount() {
		static Index id{ 0 };
		return id;
	}

	template <typename T>
	friend class impl::Pool;
	friend class Entity;

	std::shared_ptr<impl::ManagerInstance> instance_;
};

template <LoopCriterion C, typename container, typename... Ts>
class EntityContainerIterator {
public:
	using iterator_category = std::forward_iterator_tag;
	// using value_type		= std::tuple<Entity, Ts...> || Entity;
	using difference_type = std::ptrdiff_t;
	using pointer		  = Index;
	// using reference			= std::tuple<Entity, Ts&...>|| Entity;

public:
	EntityContainerIterator()										   = default;
	~EntityContainerIterator()										   = default;
	EntityContainerIterator(const EntityContainerIterator&)			   = default;
	EntityContainerIterator& operator=(const EntityContainerIterator&) = default;
	EntityContainerIterator(EntityContainerIterator&&)				   = default;
	EntityContainerIterator& operator=(EntityContainerIterator&&)	   = default;

	EntityContainerIterator& operator=(pointer entity) {
		entity_ = entity;
		return *this;
	}

	/*operator bool() const {
		if (entity_) {
			return true;
		} else {
			return false;
		}
	}*/

	bool operator==(const EntityContainerIterator& iterator) const {
		return entity_ == iterator.entity_;
	}

	bool operator!=(const EntityContainerIterator& iterator) const {
		return !(*this == iterator);
	}

	EntityContainerIterator& operator+=(const difference_type& movement) {
		entity_ += movement;
		return *this;
	}

	EntityContainerIterator& operator-=(const difference_type& movement) {
		entity_ -= movement;
		return *this;
	}

	EntityContainerIterator& operator++() {
		do {
			entity_++;
		} while (ShouldIncrement());
		return *this;
	}

	/*EntityContainerIterator& operator--() {
		--m_ptr;
		return (*this);
	}*/

	EntityContainerIterator operator++(int) {
		auto temp(*this);
		++(*this);
		return temp;
	}

	/*EntityContainerIterator operator--(int) {
		auto temp(*this);
		--m_ptr;
		return temp;
	}*/

	EntityContainerIterator operator+(const difference_type& movement) {
		auto old  = entity_;
		entity_	 += movement;
		auto temp(*this);
		entity_ = old;
		return temp;
	}

	/*EntityContainerIterator operator-(const difference_type& movement) {
		auto oldPtr	 = m_ptr;
		m_ptr		-= movement;
		auto temp(*this);
		m_ptr = oldPtr;
		return temp;
	}*/

	auto operator*() {
		return entity_container_.GetComponentTuple(entity_);
	}

	auto operator*() const {
		return entity_container_.GetComponentTuple(entity_);
	}

	pointer operator->() {
		assert(entity_container_.EntityMeetsCriteria(entity_) && "No entity with given components");
		assert(entity_container_.EntityWithinLimit(entity_) && "Out-of-range entity index");
		assert(
			!entity_container_.IsMaxEntity(entity_) &&
			"Cannot dereference entity container iterator end"
		);
		return entity_;
	}

	pointer GetEntityId() const {
		assert(entity_container_.EntityMeetsCriteria(entity_) && "No entity with given components");
		assert(entity_container_.EntityWithinLimit(entity_) && "Out-of-range entity index");
		assert(
			!entity_container_.IsMaxEntity(entity_) &&
			"Cannot dereference entity container iterator end"
		);
		return entity_;
	}

private:
	[[nodiscard]] bool ShouldIncrement() const {
		return entity_container_.EntityWithinLimit(entity_) &&
			   !entity_container_.EntityMeetsCriteria(entity_);
	}

	EntityContainerIterator(Index entity, container& entity_container) :
		entity_(entity), entity_container_{ entity_container } {
		if (ShouldIncrement()) {
			this->operator++();
		}
		if (!entity_container_.IsMaxEntity(entity_)) {
			assert(
				entity_container_.EntityWithinLimit(entity_) &&
				"Cannot create entity container iterator with out-of-range entity index"
			);
		}
	}

private:
	template <LoopCriterion T, typename... S>
	friend class EntityContainer;

	Index entity_{ 0 };
	container& entity_container_;
};

template <LoopCriterion C, typename... Ts>
class EntityContainer {
public:
	using iterator		 = EntityContainerIterator<C, EntityContainer<C, Ts...>, Ts...>;
	using const_iterator = EntityContainerIterator<C, const EntityContainer<C, Ts...>, const Ts...>;

	iterator begin() {
		return { 0, *this };
	}

	iterator end() {
		return { max_entity_, *this };
	}

	const_iterator begin() const {
		return { 0, *this };
	}

	const_iterator end() const {
		return { max_entity_, *this };
	}

	const_iterator cbegin() const {
		return { 0, *this };
	}

	const_iterator cend() const {
		return { max_entity_, *this };
	}

	void operator()(const std::function<void(Entity, Ts&...)>& func) {
		for (auto it = begin(); it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	void ForEach(const std::function<void(Entity)>& func) const {
		for (auto it = begin(); it != end(); it++) {
			std::invoke(func, GetEntity(it.GetEntityId()));
		}
	}

	[[nodiscard]] std::vector<Entity> GetVector() const {
		std::vector<Entity> v;
		v.reserve(max_entity_);
		ForEach([&](auto e) { v.push_back(e); });
		v.shrink_to_fit();
		return v;
	}

	[[nodiscard]] std::size_t Count() const {
		std::size_t count{ 0 };
		ForEach([&]([[maybe_unused]] auto e) { ++count; });
		return count;
	}

	EntityContainer(
		const Manager& manager, Index max_entity, std::tuple<impl::Pool<Ts>*...>&& pools
	) :
		manager_{ manager }, max_entity_{ max_entity }, pools_{ pools } {}

private:
	Entity GetEntity(Index entity) const;

	friend class Manager;
	template <LoopCriterion U, typename TC, typename... S>
	friend class EntityContainerIterator;

	[[nodiscard]] bool EntityMeetsCriteria(Index entity) const {
		using namespace impl; // For some reason without using namespace
							  // std::get<Pool<Ts>*>(pools_)->template Pool<Ts>::Has does not return
							  // accurate results.
		bool activated{ manager_.IsActivated(entity) };

		if (!activated) {
			return false;
		}

		if constexpr (C == LoopCriterion::None) {
			return true;
		} else { // This else suppresses unreachable code warning.
			bool pool_is_null{ ((std::get<Pool<Ts>*>(pools_) == nullptr) || ...) };

			if constexpr (C == LoopCriterion::WithComponents) {
				if (pool_is_null) {
					return false;
				}
				bool has_all_components{
					(std::get<Pool<Ts>*>(pools_)->template Pool<Ts>::Has(entity) && ...)
				};
				return has_all_components;
			}

			if constexpr (C == LoopCriterion::WithoutComponents) {
				if (pool_is_null) {
					return true;
				}
				bool missing_all_components{
					(!std::get<Pool<Ts>*>(pools_)->template Pool<Ts>::Has(entity) && ...)
				};
				return missing_all_components;
			}
		}
	}

	[[nodiscard]] bool IsMaxEntity(Index entity) const {
		return entity == max_entity_;
	}

	[[nodiscard]] bool EntityWithinLimit(Index entity) const {
		return entity < max_entity_;
	}

	[[nodiscard]] auto GetComponentTuple(Index entity);
	[[nodiscard]] auto GetComponentTuple(Index entity) const;

	Manager manager_{ 0 };
	Index max_entity_{ 0 };
	std::tuple<impl::Pool<Ts>*...> pools_;
};

class Entity {
public:
	Entity()  = default;
	~Entity() = default;

	Entity& operator=(const Entity&) = default;
	Entity(const Entity&)			 = default;
	Entity& operator=(Entity&&)		 = default;
	Entity(Entity&&)				 = default;

	bool operator==(const Entity& e) const {
		return entity_ == e.entity_ && version_ == e.version_ && manager_ == e.manager_;
	}

	bool operator!=(const Entity& e) const {
		return !(*this == e);
	}

	template <typename T, typename... Ts>
	T& Add(Ts&&... constructor_args) {
		assert(IsAlive() && "Cannot add component to dead or null entity");
		return manager_.Add<T>(entity_, manager_.GetId<T>(), std::forward<Ts>(constructor_args)...);
	}

	template <typename... Ts>
	void Remove() {
		assert(IsAlive() && "Cannot remove component(s) from dead or null entity");
		(manager_.Remove<Ts>(entity_, manager_.GetId<Ts>()), ...);
	}

	template <typename... Ts>
	[[nodiscard]] bool Has() const {
		return IsAlive() && (manager_.Has<Ts>(entity_, manager_.GetId<Ts>()) && ...);
	}

	template <typename... Ts>
	[[nodiscard]] bool HasAny() const {
		assert(IsAlive() && "Cannot check if dead or null entity has any component(s)");
		return (manager_.Has<Ts>(entity_, manager_.GetId<Ts>()) || ...);
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get() const {
		assert(IsAlive() && "Cannot get component(s) from dead or null entity");
		return manager_.Get<Ts...>(entity_);
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get() {
		assert(IsAlive() && "Cannot get component(s) from dead or null entity");
		return manager_.Get<Ts...>(entity_);
	}

	void Clear() {
		assert(IsAlive() && "Cannot clear components of dead or null entity");
		manager_.ClearEntity(entity_);
	}

	[[nodiscard]] bool IsAlive() const {
		return manager_.IsAlive(entity_, version_);
	}

	void Destroy() {
		if (manager_.IsAlive(entity_, version_)) {
			manager_.DestroyEntity(entity_, version_);
		}
	}

	[[nodiscard]] Manager& GetManager() {
		return manager_;
	}

	[[nodiscard]] const Manager& GetManager() const {
		return manager_;
	}

	[[nodiscard]] bool IsIdenticalTo(const Entity& e) const;

	[[nodiscard]] Index GetId() const {
		return entity_;
	}

	[[nodiscard]] Version GetVersion() const {
		return version_;
	};

private:
	friend class Manager;
	friend struct std::hash<Entity>;
	template <LoopCriterion C, typename... Ts>
	friend class EntityContainer;

	Entity(Index entity, Version version, const Manager& manager) :
		entity_{ entity }, version_{ version }, manager_{ manager } {}

	Index entity_{ 0 };
	Version version_{ null_version };
	Manager manager_{ 0 };
};

inline const Entity null;

template <LoopCriterion C, typename... Ts>
inline Entity EntityContainer<C, Ts...>::GetEntity(Index entity) const {
	assert(EntityWithinLimit(entity) && "Out-of-range entity index");
	assert(!IsMaxEntity(entity) && "Cannot dereference entity container iterator end");
	assert(EntityMeetsCriteria(entity) && "No entity with given components");
	return Entity{ entity, manager_.GetVersion(entity), manager_ };
}

template <LoopCriterion C, typename... Ts>
auto EntityContainer<C, Ts...>::GetComponentTuple(Index entity) {
	using namespace impl;
	assert(EntityWithinLimit(entity) && "Out-of-range entity index");
	assert(!IsMaxEntity(entity) && "Cannot dereference entity container iterator end");
	assert(EntityMeetsCriteria(entity) && "No entity with given components");
	if constexpr (C == LoopCriterion::WithComponents) {
		assert(
			((std::get<Pool<Ts>*>(pools_) != nullptr) && ...) &&
			"Component pools cannot be destroyed while looping through entities"
		);
		return std::tuple<Entity, Ts&...>(
			Entity{ entity, manager_.GetVersion(entity), manager_ },
			(std::get<Pool<Ts>*>(pools_)->template Pool<Ts>::Get(entity))...
		);
	} else {
		return Entity{ entity, manager_.GetVersion(entity), manager_ };
	}
}

template <LoopCriterion C, typename... Ts>
auto EntityContainer<C, Ts...>::GetComponentTuple(Index entity) const {
	using namespace impl;
	assert(EntityWithinLimit(entity) && "Out-of-range entity index");
	assert(!IsMaxEntity(entity) && "Cannot dereference entity container iterator end");
	assert(EntityMeetsCriteria(entity) && "No entity with given components");
	if constexpr (C == LoopCriterion::WithComponents) {
		assert(
			((std::get<Pool<Ts>*>(pools_) != nullptr) && ...) &&
			"Component pools cannot be destroyed while looping through entities"
		);
		return std::tuple<Entity, const Ts&...>(
			Entity{ entity, manager_.GetVersion(entity), manager_ },
			(std::get<Pool<Ts>*>(pools_)->template Pool<Ts>::Get(entity))...
		);
	} else {
		return Entity{ entity, manager_.GetVersion(entity), manager_ };
	}
}

inline bool Entity::IsIdenticalTo(const Entity& e) const {
	bool handle_copies{ *this == e };
	if (handle_copies) {
		return true;
	}

	bool null_entities{ *this == ecs::null && e == ecs::null };
	if (null_entities) {
		return true;
	}

	bool different_entities{ *this != ecs::null && e != ecs::null && entity_ != e.entity_ };

	return different_entities && manager_ == e.manager_ && manager_.Match(entity_, e.entity_);
}

inline Entity Manager::CreateEntity() {
	Index entity{ 0 };
	Version version{ null_version };
	GenerateEntity(entity, version);
	assert(version != null_version && "Failed to create new entity in manager");
	return Entity{ entity, version, *this };
}

template <typename... Ts>
inline void Manager::CopyEntity(const Entity& from, Entity& to) {
	CopyEntity<Ts...>(from.entity_, from.version_, to.entity_, to.version_);
}

template <typename... Ts>
inline Entity Manager::CopyEntity(const Entity& from) {
	Entity to = CreateEntity();
	CopyEntity<Ts...>(from, to);
	return to;
}

template <typename... Ts>
inline ecs::EntitiesWith<Ts...> Manager::EntitiesWith() {
	return { *this, instance_->next_entity_, std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
}

template <typename... Ts>
inline ecs::EntitiesWithout<Ts...> Manager::EntitiesWithout() {
	return { *this, instance_->next_entity_, std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
}

inline ecs::Entities Manager::Entities() {
	return { *this, instance_->next_entity_, std::make_tuple() };
}

} // namespace ecs

namespace std {

template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& e) const {
		// Source: https://stackoverflow.com/a/17017281
		std::size_t hash{ 17 };
		hash = hash * 31 + std::hash<ecs::impl::ManagerInstance*>()(e.manager_.instance_.get());
		hash = hash * 31 + std::hash<ecs::Index>()(e.entity_);
		hash = hash * 31 + std::hash<ecs::Version>()(e.version_);
		return hash;
	}
};

} // namespace std
