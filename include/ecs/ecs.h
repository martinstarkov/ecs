/*

MIT License

Copyright (c) 2025 | Martin Starkov | https://github.com/martinstarkov

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

#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef NDEBUG
#define ECS_ENABLE_ASSERTS
#endif

#ifdef ECS_ENABLE_ASSERTS

#include <cstdlib>
#include <filesystem>
#include <iostream>

#define ECS_DEBUGBREAK() ((void)0)

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
#undef ECS_DEBUGBREAK
#define ECS_DEBUGBREAK() __debugbreak()
#elif defined(__linux__)
#include <signal.h>
#undef ECS_DEBUGBREAK
#define ECS_DEBUGBREAK() raise(SIGTRAP)
#endif

#define ECS_ASSERT(condition, message)                                                          \
	{                                                                                           \
		if (!(condition)) {                                                                     \
			std::cout << "ASSERTION FAILED: "                                                   \
					  << std::filesystem::path(__FILE__).filename().string() << ":" << __LINE__ \
					  << ": " << message << "\n";                                               \
			ECS_DEBUGBREAK();                                                                   \
			std::abort();                                                                       \
		}                                                                                       \
	}

#else

#define ECS_ASSERT(...) ((void)0)

#endif

namespace ecs {

class Entity;
class Manager;

namespace impl {

using Index	  = std::uint32_t;
using Version = std::uint32_t;

enum class LoopCriterion {
	None,
	WithComponents,
	WithoutComponents
};

} // namespace impl

template <typename T, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
class EntityContainer;

template <impl::LoopCriterion Criterion, typename TContainer, typename... Ts>
class EntityContainerIterator;

template <bool is_const>
using Entities = EntityContainer<Entity, is_const, impl::LoopCriterion::None>;

template <bool is_const, typename... TComponents>
using EntitiesWith =
	EntityContainer<Entity, is_const, impl::LoopCriterion::WithComponents, TComponents...>;

template <bool is_const, typename... TComponents>
using EntitiesWithout =
	EntityContainer<Entity, is_const, impl::LoopCriterion::WithoutComponents, TComponents...>;

namespace impl {

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
struct aggregate_initializable {
	constexpr static const bool value{ std::conjunction_v<
		std::is_aggregate<Struct>, is_direct_list_initializable<Struct, Ts...>,
		std::negation<std::conjunction<
			std::bool_constant<sizeof...(Ts) == 1>,
			std::is_same<std::decay_t<std::tuple_element_t<0, std::tuple<Ts...>>>, Struct>>>> };
};

template <typename S>
struct aggregate_initializable<S> : std::false_type {};

template <typename Struct, typename... Ts>
constexpr bool is_aggregate_initializable_v = aggregate_initializable<Struct, Ts...>::value;

} // namespace tt

class AbstractPool {
public:
	virtual ~AbstractPool() = default;
	// TODO: Move to use constexpr virtual in C++20.
	[[nodiscard]] virtual bool IsCloneable() const					  = 0;
	[[nodiscard]] virtual std::unique_ptr<AbstractPool> Clone() const = 0;
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
	Pool()							 = default;
	Pool(Pool&&) noexcept			 = default;
	Pool& operator=(Pool&&) noexcept = default;
	Pool(const Pool&)				 = default;
	Pool& operator=(const Pool&)	 = default;
	~Pool() override				 = default;

	[[nodiscard]] bool IsCloneable() const final {
		return std::is_copy_constructible_v<T>;
	}

	[[nodiscard]] std::unique_ptr<AbstractPool> Clone() const final {
		// The reason this is not statically asserted is because it would disallow
		// move-only component pools.
		if constexpr (std::is_copy_constructible_v<T>) {
			auto pool{ std::make_unique<Pool<T>>() };
			pool->components_ = components_;
			pool->dense_	  = dense_;
			pool->sparse_	  = sparse_;
			return pool;
		} else {
			ECS_ASSERT(
				false, "Cannot clone component pool with a non copy constructible component"
			);
			return nullptr;
		}
	}

	void Copy(Index from_entity, Index to_entity) final {
		// Same reason as given in Clone() for why no static_assert.
		if constexpr (std::is_copy_constructible_v<T>) {
			ECS_ASSERT(
				Has(from_entity), "Cannot copy from an entity which does not exist in the manager"
			);
			if (!Has(to_entity)) {
				Add(to_entity, components_[sparse_[from_entity]]);
			} else {
				components_.emplace(
					components_.begin() + sparse_[to_entity], components_[sparse_[from_entity]]
				);
			}
		} else {
			ECS_ASSERT(false, "Cannot copy an entity with a non copy constructible component");
		}
	}

	void Clear() final {
		components_.clear();
		dense_.clear();
		sparse_.clear();
	}

	void Reset() final {
		Clear();

		components_.shrink_to_fit();
		dense_.shrink_to_fit();
		sparse_.shrink_to_fit();
	}

	bool Remove(Index entity) final {
		if (!Has(entity)) {
			return false;
		}
		// See https://skypjack.github.io/2020-08-02-ecs-baf-part-9/ for
		// in-depth explanation. In short, swap with back and pop back,
		// relinking sparse ids after.
		auto last{ dense_.back() };
		std::swap(dense_.back(), dense_[sparse_[entity]]);
		std::swap(components_.back(), components_[sparse_[entity]]);
		ECS_ASSERT(last < sparse_.size(), "");
		std::swap(sparse_[last], sparse_[entity]);
		dense_.pop_back();
		components_.pop_back();
		return true;
	}

	[[nodiscard]] bool Has(Index entity) const final {
		if (entity >= sparse_.size()) {
			return false;
		}
		auto s{ sparse_[entity] };
		if (s >= dense_.size()) {
			return false;
		}
		return entity == dense_[s];
	}

	[[nodiscard]] const T& Get(Index entity) const {
		ECS_ASSERT(Has(entity), "Entity does not have the requested component");
		ECS_ASSERT(
			sparse_[entity] < components_.size(),
			"Likely attempting to retrieve a component before it has been fully "
			"added to the "
			"entity, e.g. self-referencing Get() in the Add() constructor call"
		);
		return components_[sparse_[entity]];
	}

	[[nodiscard]] T& Get(Index entity) {
		return const_cast<T&>(std::as_const(*this).Get(entity));
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

template <typename T, bool is_const, typename... Ts>
class Pools {
public:
	template <typename T>
	using PoolType = std::conditional_t<is_const, const Pool<T>*, Pool<T>*>;

	explicit constexpr Pools(PoolType<Ts>... pools) :
		pools_{ std::tuple<PoolType<Ts>...>(pools...) } {}

	constexpr void Copy(Index from_id, Index to_id) {
		(std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Copy(from_id, to_id), ...);
	}

	[[nodiscard]] constexpr bool Has(Index entity) const {
		return AllExist() &&
			   (std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Has(entity) && ...);
	}

	[[nodiscard]] constexpr bool NotHas(Index entity) const {
		return ((std::get<PoolType<Ts>>(pools_) == nullptr) || ...) ||
			   (!std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Has(entity) && ...);
	}

	[[nodiscard]] constexpr decltype(auto) GetWithEntity(Index entity, const Manager* manager)
		const;

	[[nodiscard]] constexpr decltype(auto) GetWithEntity(Index entity, Manager* manager);

	[[nodiscard]] constexpr decltype(auto) Get(Index entity) const {
		ECS_ASSERT(AllExist(), "Manager does not have at least one of the requested components");
		static_assert(sizeof...(Ts) > 0);
		if constexpr (sizeof...(Ts) == 1) {
			return (std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Get(entity), ...);
		} else {
			return std::forward_as_tuple<const Ts&...>(
				(std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Get(entity))...
			);
		}
	}

	[[nodiscard]] constexpr decltype(auto) Get(Index entity) {
		ECS_ASSERT(AllExist(), "Manager does not have at least one of the requested components");
		static_assert(sizeof...(Ts) > 0);
		if constexpr (sizeof...(Ts) == 1) {
			return (std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Get(entity), ...);
		} else {
			return std::forward_as_tuple<Ts&...>(
				(std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Get(entity))...
			);
		}
	}

	[[nodiscard]] constexpr bool AllExist() const {
		return ((std::get<PoolType<Ts>>(pools_) != nullptr) && ...);
	}

private:
	std::tuple<PoolType<Ts>...> pools_;
};

class DynamicBitset {
	// Modified version of:
	// https://github.com/syoyo/dynamic_bitset/blob/master/dynamic_bitset.hh
public:
	void Set(std::size_t index, bool value = true) {
		std::size_t byte_index{ index / 8 };
		std::uint8_t offset{ static_cast<std::uint8_t>(index % 8) };
		std::uint8_t bitfield{ static_cast<std::uint8_t>(1 << offset) };

		ECS_ASSERT(byte_index < data_.size(), "");

		if (value) {
			data_[byte_index] |= bitfield;
		} else {
			data_[byte_index] &= (~bitfield);
		}
	}

	[[nodiscard]] bool operator[](std::size_t index) const {
		std::size_t byte_index{ index / 8 };
		std::size_t offset{ index % 8 };

		ECS_ASSERT(byte_index < data_.size(), "");
		int set{ (data_[byte_index] >> offset) & 0x1 };
		return static_cast<bool>(set);
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
		auto byte_count{ GetByteCount(new_capacity) };
		data_.reserve(byte_count);
	}

	void Resize(std::size_t new_size, bool value) {
		auto byte_count{ GetByteCount(new_size) };
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
	[[nodiscard]] static std::size_t GetByteCount(std::size_t bit_count) {
		std::size_t byte_count{ 1 };
		if (bit_count >= 8) {
			ECS_ASSERT(1 + (bit_count - 1) / 8 > 0, "");
			byte_count = 1 + (bit_count - 1) / 8;
		}
		return byte_count;
	}

	std::size_t bit_count_{ 0 };
	// TODO: Move to std::byte instead of std::uint8_t.
	std::vector<std::uint8_t> data_;
};

} // namespace impl

class Manager {
public:
	Manager() = default;

	Manager(const Manager&)			   = delete;
	Manager& operator=(const Manager&) = delete;

	Manager(Manager&& other) noexcept :
		next_entity_{ std::exchange(next_entity_, 0) },
		count_{ std::exchange(other.count_, 0) },
		refresh_required_{ std::exchange(other.refresh_required_, false) },
		entities_{ std::exchange(other.entities_, {}) },
		refresh_{ std::exchange(other.refresh_, {}) },
		versions_{ std::exchange(other.versions_, {}) },
		free_entities_{ std::exchange(other.free_entities_, {}) },
		pools_{ std::exchange(other.pools_, {}) } {}

	Manager& operator=(Manager&& other) noexcept {
		if (this != &other) {
			next_entity_	  = std::exchange(next_entity_, 0);
			count_			  = std::exchange(other.count_, 0);
			refresh_required_ = std::exchange(other.refresh_required_, false);
			entities_		  = std::exchange(other.entities_, {});
			refresh_		  = std::exchange(other.refresh_, {});
			versions_		  = std::exchange(other.versions_, {});
			free_entities_	  = std::exchange(other.free_entities_, {});
			pools_			  = std::exchange(other.pools_, {});
		}
		return *this;
	}

	~Manager() = default;

	friend bool operator==(const Manager& a, const Manager& b) {
		return &a == &b;
	}

	friend bool operator!=(const Manager& a, const Manager& b) {
		return !operator==(a, b);
	}

	[[nodiscard]] Manager Clone() const {
		Manager clone;
		clone.count_			= count_;
		clone.next_entity_		= next_entity_;
		clone.entities_			= entities_;
		clone.refresh_			= refresh_;
		clone.refresh_required_ = refresh_required_;
		clone.versions_			= versions_;
		clone.free_entities_	= free_entities_;

		clone.pools_.resize(pools_.size());
		for (std::size_t i{ 0 }; i < pools_.size(); ++i) {
			const auto& pool{ pools_[i] };
			if (pool == nullptr) {
				continue;
			}
			clone.pools_[i] = pool->Clone();
			ECS_ASSERT(clone.pools_[i] != nullptr, "Cloning manager failed");
		}
		return clone;
	}

	void Refresh() {
		if (!refresh_required_) {
			return;
		}
		// This must be set before refresh starts in case
		// events are called (for instance during entity deletion).
		refresh_required_ = false;
		ECS_ASSERT(
			entities_.Size() == versions_.size(),
			"Refresh failed due to varying entity vector and version vector "
			"size"
		);
		ECS_ASSERT(
			entities_.Size() == refresh_.Size(),
			"Refresh failed due to varying entity vector and refresh vector "
			"size"
		);
		ECS_ASSERT(
			next_entity_ <= entities_.Size(),
			"Next available entity must not be out of bounds of entity vector"
		);
		impl::Index alive{ 0 };
		impl::Index dead{ 0 };
		for (impl::Index entity{ 0 }; entity < next_entity_; ++entity) {
			if (!refresh_[entity]) {
				continue;
			}
			// Entity was marked for refresh.
			refresh_.Set(entity, false);
			if (entities_[entity]) { // Marked for deletion.
				ClearEntity(entity);
				entities_.Set(entity, false);
				++versions_[entity];
				free_entities_.emplace_back(entity);
				++dead;
			} else { // Marked for 'creation'.
				entities_.Set(entity, true);
				++alive;
			}
		}
		// Update entity count with net change.
		count_ += alive;
		count_	= count_ > dead ? count_ - dead : 0;
	}

	void Reserve(std::size_t capacity) {
		entities_.Reserve(capacity);
		refresh_.Reserve(capacity);
		versions_.reserve(capacity);
		ECS_ASSERT(
			entities_.Capacity() == refresh_.Capacity(),
			"Entity and refresh vectors must have the same capacity"
		);
	}

	// Make sure to call Refresh() after this function.
	Entity CreateEntity();

	template <typename... Ts>
	void CopyEntity(const Entity& from, Entity& to);

	// Make sure to call Refresh() after this function.
	template <typename... Ts>
	Entity CopyEntity(const Entity& from);

	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWith<true, Ts...> EntitiesWith() const;

	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWith<false, Ts...> EntitiesWith();

	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWithout<true, Ts...> EntitiesWithout() const;

	template <typename... Ts>
	[[nodiscard]] ecs::EntitiesWithout<false, Ts...> EntitiesWithout();

	[[nodiscard]] ecs::Entities<true> Entities() const;

	[[nodiscard]] ecs::Entities<false> Entities();

	[[nodiscard]] std::size_t Size() const {
		return count_;
	}

	[[nodiscard]] bool IsEmpty() const {
		return Size() == 0;
	}

	[[nodiscard]] std::size_t Capacity() const {
		return versions_.capacity();
	}

	void Clear() {
		count_			  = 0;
		next_entity_	  = 0;
		refresh_required_ = false;

		entities_.Clear();
		refresh_.Clear();
		versions_.clear();
		free_entities_.clear();

		for (const auto& pool : pools_) {
			if (pool != nullptr) {
				pool->Clear();
			}
		}
	}

	void Reset() {
		Clear();

		entities_.ShrinkToFit();
		refresh_.ShrinkToFit();
		versions_.shrink_to_fit();
		free_entities_.shrink_to_fit();

		pools_.clear();
		pools_.shrink_to_fit();
	}

protected:
	friend struct std::hash<Entity>;
	friend class Entity;

	template <typename T, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
	friend class EntityContainer;
	friend class Entity;

	template <typename T, bool is_const, typename... Ts>
	friend class impl::Pools;

	template <typename... Ts>
	void CopyEntity(
		impl::Index from_id, impl::Version from_version, impl::Index to_id, impl::Version to_version
	) {
		ECS_ASSERT(
			IsAlive(from_id, from_version),
			"Cannot copy from entity which has not been initialized from the "
			"manager"
		);
		ECS_ASSERT(
			IsAlive(to_id, to_version),
			"Cannot copy to entity which has not been initialized from the "
			"manager"
		);
		if constexpr (sizeof...(Ts) > 0) { // Copy only specific components.
			static_assert(
				std::conjunction_v<std::is_copy_constructible<Ts>...>,
				"Cannot copy entity with a component that is not copy constructible"
			);
			impl::Pools<Entity, false, Ts...> pools{ GetPool<Ts>(GetId<Ts>())... };
			ECS_ASSERT(
				pools.AllExist(), "Cannot copy entity with a component that is not "
								  "even in the manager"
			);
			ECS_ASSERT(
				pools.Has(from_id), "Cannot copy entity with a component that it does not have"
			);
			pools.Copy(from_id, to_id);
		} else { // Copy all components.
			for (auto& pool : pools_) {
				if (pool != nullptr && pool->Has(from_id)) {
					pool->Copy(from_id, to_id);
				}
			}
		}
	}

	void GenerateEntity(impl::Index& entity, impl::Version& version) {
		entity = 0;
		// Pick entity from free list before trying to increment entity counter.
		if (!free_entities_.empty()) {
			entity = free_entities_.front();
			free_entities_.pop_front();
		} else {
			entity = next_entity_++;
		}
		// Double the size of the manager if capacity is reached.
		if (entity >= entities_.Size()) {
			if (versions_.capacity() == 0) {
				// Reserve capacity for 1 entity so that manager size will double in powers of 2.
				Reserve(1);
			}
			Resize(versions_.capacity() * 2);
		}
		ECS_ASSERT(
			entity < entities_.Size(), "Created entity is outside of manager entity vector range"
		);
		ECS_ASSERT(!entities_[entity], "Cannot create new entity from live entity");
		ECS_ASSERT(!refresh_[entity], "Cannot create new entity from refresh marked entity");
		// Mark entity for refresh.
		refresh_.Set(entity, true);
		refresh_required_ = true;
		// Entity version incremented here.
		version = ++versions_[entity];
	}

	void Resize(std::size_t size) {
		if (size > entities_.Size()) {
			entities_.Resize(size, false);
			refresh_.Resize(size, false);
			versions_.resize(size, 0);
		}
		ECS_ASSERT(
			entities_.Size() == versions_.size(),
			"Resize failed due to varying entity vector and version vector size"
		);
		ECS_ASSERT(
			entities_.Size() == refresh_.Size(),
			"Resize failed due to varying entity vector and refresh vector size"
		);
	}

	void ClearEntity(impl::Index entity) const {
		for (const auto& pool : pools_) {
			if (pool != nullptr) {
				pool->Remove(entity);
			}
		}
	}

	[[nodiscard]] bool IsAlive(impl::Index entity, impl::Version version) const {
		return version != 0 && entity < versions_.size() && versions_[entity] == version &&
			   entity < entities_.Size() &&
			   // Entity considered currently alive or entity marked
			   // for creation/deletion but not yet created/deleted.
			   (entities_[entity] || refresh_[entity]);
	}

	[[nodiscard]] bool IsActivated(impl::Index entity) const {
		return entity < entities_.Size() && entities_[entity];
	}

	[[nodiscard]] impl::Version GetVersion(impl::Index entity) const {
		ECS_ASSERT(entity < versions_.size(), "");
		return versions_[entity];
	}

	[[nodiscard]] bool Match(impl::Index entity1, impl::Index entity2) const {
		for (const auto& pool : pools_) {
			if (pool == nullptr) {
				continue;
			}
			bool has1{ pool->Has(entity1) };
			bool has2{ pool->Has(entity2) };
			// Check that one entity has a component while the other doesn't.
			if ((has1 || has2) && (!has1 || !has2)) {
				// Exit early if one non-matching component is found.
				return false;
			}
		}
		return true;
	}

	void DestroyEntity(impl::Index entity, impl::Version version) {
		ECS_ASSERT(entity < versions_.size(), "");
		ECS_ASSERT(entity < refresh_.Size(), "");
		if (versions_[entity] != version) {
			return;
		}
		if (entities_[entity]) {
			refresh_.Set(entity, true);
			refresh_required_ = true;
		} else if (refresh_[entity]) {
			/*
			 * Edge case where entity is created and marked for deletion
			 * before a Refresh() has been called.
			 * In this case, destroy and invalidate the entity without
			 * a Refresh() call. This is equivalent to an entity which
			 * never 'officially' existed in the manager.
			 */
			ClearEntity(entity);
			refresh_.Set(entity, false);
			++versions_[entity];
			free_entities_.emplace_back(entity);
		}
	}

	template <typename T>
	[[nodiscard]] const impl::Pool<T>* GetPool(impl::Index component) const {
		ECS_ASSERT(component == GetId<T>(), "GetPool mismatch with component id");
		if (component < pools_.size()) {
			const auto& pool{ pools_[component] };
			// This is nullptr if the pool does not exist in the manager.
			return static_cast<impl::Pool<T>*>(pool.get());
		}
		return nullptr;
	}

	template <typename T>
	[[nodiscard]] impl::Pool<T>* GetPool(impl::Index component) {
		return const_cast<impl::Pool<T>*>(std::as_const(*this).GetPool<T>(component));
	}

	template <typename T>
	void Remove(impl::Index entity, impl::Index component) {
		auto pool{ GetPool<T>(component) };
		if (pool != nullptr) {
			pool->template Pool<T>::Remove(entity);
		}
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get(impl::Index entity) const {
		impl::Pools<Entity, true, Ts...> p{ GetPool<Ts...>(GetId<Ts...>())... };
		return p.Get(entity);
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get(impl::Index entity) {
		impl::Pools<Entity, false, Ts...> p{ GetPool<Ts>(GetId<Ts>())... };
		return p.Get(entity);
	}

	template <typename T>
	[[nodiscard]] bool Has(impl::Index entity, impl::Index component) const {
		const auto pool{ GetPool<T>(component) };
		return pool != nullptr && pool->Has(entity);
	}

	template <typename T, typename... Ts>
	T& Add(impl::Index entity, impl::Index component, Ts&&... constructor_args) {
		if (component >= pools_.size()) {
			pools_.resize(static_cast<std::size_t>(component) + 1);
		}
		impl::Pool<T>* pool{ GetPool<T>(component) };
		if (pool == nullptr) {
			auto new_pool{ std::make_unique<impl::Pool<T>>() };
			pool = new_pool.get();
			ECS_ASSERT(component < pools_.size(), "Component index out of range");
			pools_[component] = std::move(new_pool);
		}
		ECS_ASSERT(pool != nullptr, "Could not create new component pool correctly");
		return pool->Add(entity, std::forward<Ts>(constructor_args)...);
	}

	template <typename T>
	[[nodiscard]] static impl::Index GetId() {
		// Get the next available id save that id as static variable for the
		// component type.
		static impl::Index id{ ComponentCount()++ };
		return id;
	}

	[[nodiscard]] static impl::Index& ComponentCount() {
		static impl::Index id{ 0 };
		return id;
	}

	template <typename T>
	friend class impl::Pool;
	friend class Entity;

	impl::Index next_entity_{ 0 };
	impl::Index count_{ 0 };
	bool refresh_required_{ false };
	impl::DynamicBitset entities_;
	impl::DynamicBitset refresh_;
	std::vector<impl::Version> versions_;
	std::deque<impl::Index> free_entities_;
	std::vector<std::unique_ptr<impl::AbstractPool>> pools_;
};

class Entity {
public:
	Entity() = default;

	Entity(const Entity&)			 = default;
	Entity& operator=(const Entity&) = default;

	Entity(Entity&& other) noexcept :
		entity_{ std::exchange(other.entity_, 0) },
		version_{ std::exchange(other.version_, 0) },
		manager_{ std::exchange(other.manager_, nullptr) } {}

	Entity& operator=(Entity&& other) noexcept {
		if (this != &other) {
			entity_	 = std::exchange(other.entity_, 0);
			version_ = std::exchange(other.version_, 0);
			manager_ = std::exchange(other.manager_, nullptr);
		}
		return *this;
	}

	~Entity() = default;

	friend bool operator==(const Entity& a, const Entity& b) {
		return a.entity_ == b.entity_ && a.version_ == b.version_ && a.manager_ == b.manager_;
	}

	friend bool operator!=(const Entity& a, const Entity& b) {
		return !(a == b);
	}

	// Copying a destroyed entity will return Entity{}.
	// Copying an entity with no components simply returns a new entity.
	// Make sure to call manager.Refresh() after this function.
	template <typename... Ts>
	Entity Copy() {
		if (manager_ == nullptr) {
			return {};
		}
		return manager_->CopyEntity<Ts...>(*this);
	}

	template <typename T, typename... Ts>
	T& Add(Ts&&... constructor_args) {
		ECS_ASSERT(manager_ != nullptr, "Cannot add component to null entity");
		return manager_->Add<T>(
			entity_, manager_->GetId<T>(), std::forward<Ts>(constructor_args)...
		);
	}

	template <typename... Ts>
	void Remove() {
		if (manager_ == nullptr) {
			return;
		}
		(manager_->Remove<Ts>(entity_, manager_->GetId<Ts>()), ...);
	}

	template <typename... Ts>
	[[nodiscard]] bool Has() const {
		return manager_ != nullptr && (manager_->Has<Ts>(entity_, manager_->GetId<Ts>()) && ...);
	}

	template <typename... Ts>
	[[nodiscard]] bool HasAny() const {
		return manager_ != nullptr && (manager_->Has<Ts>(entity_, manager_->GetId<Ts>()) || ...);
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of null entity");
		return manager_->Get<Ts...>(entity_);
	}

	template <typename... Ts>
	[[nodiscard]] decltype(auto) Get() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get component of null entity");
		return manager_->Get<Ts...>(entity_);
	}

	void Clear() const {
		if (manager_ == nullptr) {
			return;
		}
		manager_->ClearEntity(entity_);
	}

	[[nodiscard]] bool IsAlive() const {
		return manager_ != nullptr && manager_->IsAlive(entity_, version_);
	}

	void Destroy() {
		if (manager_ != nullptr && manager_->IsAlive(entity_, version_)) {
			manager_->DestroyEntity(entity_, version_);
		}
	}

	[[nodiscard]] Manager& GetManager() {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of null entity");
		return *manager_;
	}

	[[nodiscard]] const Manager& GetManager() const {
		ECS_ASSERT(manager_ != nullptr, "Cannot get manager of null entity");
		return *manager_;
	}

	[[nodiscard]] bool IsIdenticalTo(const Entity& e) const {
		if (*this == e) {
			return true;
		}

		return entity_ != e.entity_ && manager_ == e.manager_ && manager_ != nullptr
				 ? manager_->Match(entity_, e.entity_)
				 : true;
	}

protected:
	friend class Manager;
	friend struct std::hash<Entity>;
	template <typename T, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
	friend class EntityContainer;
	template <typename T, bool is_const, typename... Ts>
	friend class impl::Pools;

	Entity(impl::Index entity, impl::Version version, const Manager* manager) :
		entity_{ entity }, version_{ version }, manager_{ const_cast<Manager*>(manager) } {}

	Entity(impl::Index entity, impl::Version version, Manager* manager) :
		entity_{ entity }, version_{ version }, manager_{ manager } {}

	impl::Index entity_{ 0 };
	impl::Version version_{ 0 };
	Manager* manager_{ nullptr };
};

inline const Entity null{};

template <impl::LoopCriterion Criterion, typename TContainer, typename... Ts>
class EntityContainerIterator {
public:
	using iterator_category = std::forward_iterator_tag;
	using difference_type	= std::ptrdiff_t;
	using pointer			= impl::Index;
	// using value_type		= std::tuple<Entity, Ts...> || Entity;
	// using reference			= std::tuple<Entity, Ts&...>|| Entity;

	EntityContainerIterator() = default;

	EntityContainerIterator& operator=(pointer entity) {
		entity_ = entity;
		return *this;
	}

	friend bool operator==(const EntityContainerIterator& a, const EntityContainerIterator& b) {
		return a.entity_ == b.entity_;
	}

	friend bool operator!=(const EntityContainerIterator& a, const EntityContainerIterator& b) {
		return !(a == b);
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

	EntityContainerIterator operator++(int) {
		auto temp(*this);
		++(*this);
		return temp;
	}

	EntityContainerIterator operator+(const difference_type& movement) {
		auto old  = entity_;
		entity_	 += movement;
		auto temp(*this);
		entity_ = old;
		return temp;
	}

	decltype(auto) operator*() const {
		return entity_container_.GetComponentTuple(entity_);
	}

	pointer operator->() const {
		ECS_ASSERT(
			entity_container_.EntityMeetsCriteria(entity_), "No entity with given components"
		);
		ECS_ASSERT(entity_container_.EntityWithinLimit(entity_), "Out-of-range entity index");
		ECS_ASSERT(
			!entity_container_.IsMaxEntity(entity_),
			"Cannot dereference entity container iterator end"
		);
		return entity_;
	}

	pointer GetEntityId() const {
		ECS_ASSERT(
			entity_container_.EntityMeetsCriteria(entity_), "No entity with given components"
		);
		ECS_ASSERT(entity_container_.EntityWithinLimit(entity_), "Out-of-range entity index");
		ECS_ASSERT(
			!entity_container_.IsMaxEntity(entity_),
			"Cannot dereference entity container iterator end"
		);
		return entity_;
	}

private:
	[[nodiscard]] bool ShouldIncrement() const {
		return entity_container_.EntityWithinLimit(entity_) &&
			   !entity_container_.EntityMeetsCriteria(entity_);
	}

	EntityContainerIterator(impl::Index entity, TContainer entity_container) :
		entity_(entity), entity_container_{ entity_container } {
		if (ShouldIncrement()) {
			this->operator++();
		}
		if (!entity_container_.IsMaxEntity(entity_)) {
			ECS_ASSERT(
				entity_container_.EntityWithinLimit(entity_),
				"Cannot create entity container iterator with out-of-range entity "
				"index"
			);
		}
	}

private:
	template <typename T, bool is_const, impl::LoopCriterion C, typename... S>
	friend class EntityContainer;

	impl::Index entity_{ 0 };
	TContainer entity_container_;
};

template <typename T, bool is_const, impl::LoopCriterion Criterion, typename... Ts>
class EntityContainer {
public:
	using ManagerType = std::conditional_t<is_const, const Manager*, Manager*>;

	EntityContainer() = default;

	EntityContainer(
		ManagerType manager, impl::Index max_entity, const impl::Pools<T, is_const, Ts...>& pools
	) :
		manager_{ manager }, max_entity_{ max_entity }, pools_{ pools } {}

	using iterator =
		EntityContainerIterator<Criterion, EntityContainer<T, is_const, Criterion, Ts...>&, Ts...>;
	using const_iterator = EntityContainerIterator<
		Criterion, const EntityContainer<T, is_const, Criterion, Ts...>&, Ts...>;

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

	template <bool IS_CONST = is_const, std::enable_if_t<IS_CONST, int> = 0>
	void operator()(const std::function<void(T, const Ts&...)>& func) const {
		for (auto it{ begin() }; it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	template <bool IS_CONST = is_const, std::enable_if_t<!IS_CONST, int> = 0>
	void operator()(const std::function<void(T, Ts&...)>& func) {
		for (auto it{ begin() }; it != end(); it++) {
			std::apply(func, GetComponentTuple(it.GetEntityId()));
		}
	}

	void ForEach(const std::function<void(T)>& func) const {
		for (auto it{ begin() }; it != end(); it++) {
			std::invoke(func, GetEntity(it.GetEntityId()));
		}
	}

	[[nodiscard]] std::vector<T> GetVector() const {
		std::vector<T> v;
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

private:
	T GetEntity(impl::Index entity) const {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity container iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		return T{ entity, manager_->GetVersion(entity), manager_ };
	}

	friend class Manager;
	template <impl::LoopCriterion U, typename TContainer, typename... S>
	friend class EntityContainerIterator;

	[[nodiscard]] bool EntityMeetsCriteria(impl::Index entity) const {
		bool activated{ manager_->IsActivated(entity) };

		if (!activated) {
			return false;
		}

		if constexpr (Criterion == impl::LoopCriterion::None) {
			return true;
		} else { // This else suppresses unreachable code warning.
			if constexpr (Criterion == impl::LoopCriterion::WithComponents) {
				return pools_.Has(entity);
			}

			if constexpr (Criterion == impl::LoopCriterion::WithoutComponents) {
				return pools_.NotHas(entity);
			}
		}
	}

	[[nodiscard]] bool IsMaxEntity(impl::Index entity) const {
		return entity == max_entity_;
	}

	[[nodiscard]] bool EntityWithinLimit(impl::Index entity) const {
		return entity < max_entity_;
	}

	template <bool IS_CONST = is_const, std::enable_if_t<IS_CONST, int> = 0>
	[[nodiscard]] decltype(auto) GetComponentTuple(impl::Index entity) const {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity container iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		if constexpr (Criterion == impl::LoopCriterion::WithComponents) {
			impl::Pools<T, true, Ts...> pools{ manager_->GetPool<Ts>(manager_->GetId<Ts>())... };
			return pools.GetWithEntity(entity, manager_);
		} else {
			return T{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	template <bool IS_CONST = is_const, std::enable_if_t<!IS_CONST, int> = 0>
	[[nodiscard]] decltype(auto) GetComponentTuple(impl::Index entity) {
		ECS_ASSERT(EntityWithinLimit(entity), "Out-of-range entity index");
		ECS_ASSERT(!IsMaxEntity(entity), "Cannot dereference entity container iterator end");
		ECS_ASSERT(EntityMeetsCriteria(entity), "No entity with given components");
		if constexpr (Criterion == impl::LoopCriterion::WithComponents) {
			impl::Pools<T, false, Ts...> pools{ manager_->GetPool<Ts>(manager_->GetId<Ts>())... };
			return pools.GetWithEntity(entity, manager_);
		} else {
			return T{ entity, manager_->GetVersion(entity), manager_ };
		}
	}

	ManagerType manager_{ nullptr };
	impl::Index max_entity_{ 0 };
	impl::Pools<T, is_const, Ts...> pools_;
};

namespace impl {

template <typename T, bool is_const, typename... Ts>
[[nodiscard]] constexpr decltype(auto) Pools<T, is_const, Ts...>::GetWithEntity(
	Index entity, const Manager* manager
) const {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(Ts) > 0);
	return std::tuple<T, const Ts&...>(
		T{ entity, manager->GetVersion(entity), manager },
		(std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Get(entity))...
	);
}

template <typename T, bool is_const, typename... Ts>
[[nodiscard]] constexpr decltype(auto) Pools<T, is_const, Ts...>::GetWithEntity(
	Index entity, Manager* manager
) {
	ECS_ASSERT(AllExist(), "Component pools cannot be destroyed while looping through entities");
	static_assert(sizeof...(Ts) > 0);
	return std::tuple<T, Ts&...>(
		T{ entity, manager->GetVersion(entity), manager },
		(std::get<PoolType<Ts>>(pools_)->template Pool<Ts>::Get(entity))...
	);
}

} // namespace impl

inline Entity Manager::CreateEntity() {
	impl::Index entity{ 0 };
	impl::Version version{ 0 };
	GenerateEntity(entity, version);
	ECS_ASSERT(version != 0, "Failed to create new entity in manager");
	return Entity{ entity, version, this };
}

template <typename... Ts>
inline void Manager::CopyEntity(const Entity& from, Entity& to) {
	CopyEntity<Ts...>(from.entity_, from.version_, to.entity_, to.version_);
}

template <typename... Ts>
inline Entity Manager::CopyEntity(const Entity& from) {
	Entity to{ CreateEntity() };
	CopyEntity<Ts...>(from, to);
	return to;
}

template <typename... Ts>
inline ecs::EntitiesWith<true, Ts...> Manager::EntitiesWith() const {
	return { this, next_entity_, impl::Pools<Entity, true, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename... Ts>
inline ecs::EntitiesWith<false, Ts...> Manager::EntitiesWith() {
	return { this, next_entity_, impl::Pools<Entity, false, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename... Ts>
inline ecs::EntitiesWithout<true, Ts...> Manager::EntitiesWithout() const {
	return { this, next_entity_, impl::Pools<Entity, true, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

template <typename... Ts>
inline ecs::EntitiesWithout<false, Ts...> Manager::EntitiesWithout() {
	return { this, next_entity_, impl::Pools<Entity, false, Ts...>{ GetPool<Ts>(GetId<Ts>())... } };
}

inline ecs::Entities<true> Manager::Entities() const {
	return { this, next_entity_, impl::Pools<Entity, true>{} };
}

inline ecs::Entities<false> Manager::Entities() {
	return { this, next_entity_, impl::Pools<Entity, false>{} };
}

} // namespace ecs

namespace std {

template <>
struct hash<ecs::Entity> {
	size_t operator()(const ecs::Entity& e) const {
		// Source: https://stackoverflow.com/a/17017281
		size_t h{ 17 };
		h = h * 31 + hash<ecs::Manager*>()(e.manager_);
		h = h * 31 + hash<ecs::impl::Index>()(e.entity_);
		h = h * 31 + hash<ecs::impl::Version>()(e.version_);
		return h;
	}
};

} // namespace std