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
#include <memory>
#include <type_traits>
#include <vector>

namespace ecs {

class Entity;
class Manager;

template <typename... Ts>
class System;
template <typename... Ts>
class SystemIterator;

namespace impl {

class NullEntity;

using Index	  = std::size_t;
using Version = std::uint32_t;

inline constexpr Version null_version{ 0 };

class PoolInterface {
public:
	virtual ~PoolInterface()								   = default;
	virtual std::shared_ptr<impl::PoolInterface> Clone() const = 0;
	virtual void Copy(Index from_entity, Index to_entity)	   = 0;
	virtual void Clear()									   = 0;
	virtual void Reset()									   = 0;
	virtual bool Remove(Index entity)						   = 0;
	virtual bool Has(Index entity) const					   = 0;
	virtual Index GetId() const								   = 0;
};

template <typename T>
class Pool : public PoolInterface {
public:
	Pool() = default;

	Pool(
		const std::vector<T>& components, const std::vector<Index>& dense,
		const std::vector<Index>& sparse
	) :
		components{ components }, dense{ dense }, sparse{ sparse } {}

	~Pool()						 = default;
	Pool(Pool&&)				 = default;
	Pool& operator=(Pool&&)		 = default;
	Pool(const Pool&)			 = default;
	Pool& operator=(const Pool&) = default;

	virtual std::shared_ptr<impl::PoolInterface> Clone() const final {
		static_assert(
			std::is_copy_constructible_v<T>,
			"Cannot clone component pool with a non copy-constructible component"
		);
		return std::make_shared<Pool<T>>(components, dense, sparse);
	}

	virtual void Copy(Index from_entity, Index to_entity) final {
		static_assert(
			std::is_copy_constructible_v<T>,
			"Cannot copy component in a pool of non copy constructible components"
		);
		assert(Has(from_entity));
		if (Has(to_entity)) {
			components[sparse[to_entity]] = components[sparse[from_entity]];
		} else {
			Add(to_entity, components[sparse[from_entity]]);
		}
	}

	virtual void Clear() final {
		components.clear();
		dense.clear();
		sparse.clear();
	}

	virtual void Reset() final {
		Clear();

		components.shrink_to_fit();
		dense.shrink_to_fit();
		sparse.shrink_to_fit();
	}

	virtual bool Remove(Index entity) final {
		if (Has(entity)) {
			// See https://skypjack.github.io/2020-08-02-ecs-baf-part-9/ for
			// in-depth explanation. In short, swap with back and pop back,
			// relinking sparse ids after.
			const Index last{ dense.back() };
			std::swap(dense.back(), dense[sparse[entity]]);
			std::swap(components.back(), components[sparse[entity]]);
			assert(last < sparse.size());
			std::swap(sparse[last], sparse[entity]);
			dense.pop_back();
			components.pop_back();
			return true;
		}
		return false;
	}

	virtual bool Has(Index entity) const final {
		if (entity >= sparse.size()) {
			return false;
		}
		auto s = sparse[entity];
		if (s >= dense.size()) {
			return false;
		}
		return entity == dense[s];
	}

	virtual Index GetId() const final;

	const T& Get(Index entity) const {
		/*
		 * Debug tip:
		 * If you ended up here and want to find out which entity triggered
		 * this assertion, set breakpoints or follow the call stack.
		 */
		assert(Has(entity) && "Cannot get a component which an entity does not have");
		return components[sparse[entity]];
	}

	T& Get(Index entity) {
		return const_cast<T&>(static_cast<const Pool<T>&>(*this).Get(entity));
	}

	template <typename... Ts>
	T& Add(Index entity, Ts&&... constructor_args) {
		static_assert(
			std::is_constructible_v<T, Ts...>,
			"Cannot add component which is not constructible from given arguments"
		);
		static_assert(
			std::is_move_constructible_v<T>, "Cannot add component which is not move constructible"
		);
		static_assert(std::is_destructible_v<T>, "Cannot add component which is not destructible");
		if (entity < sparse.size()) {			   // Entity has had the component before.
			if (sparse[entity] < dense.size() &&
				dense[sparse[entity]] == entity) { // Entity currently has the component.
				// Replace the current component with a new component.
				T& component{ components[sparse[entity]] };
				// This approach prevents the creation of a temporary component
				// object.
				component.~T();
				new (&component) T(std::forward<Ts>(constructor_args)...);
				return component;
			}
			// Entity currently does not have the component.
			sparse[entity] = dense.size();
		} else {
			// Entity has never had the component.
			sparse.resize(entity + 1, dense.size());
		}
		// Add new component to the entity.
		dense.push_back(entity);
		return components.emplace_back(std::forward<Ts>(constructor_args)...);
	}

private:
	std::vector<T> components;
	std::vector<Index> dense;
	std::vector<Index> sparse;
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
		std::uint8_t offset{ index % 8 };
		std::uint8_t bitfield = std::uint8_t(1 << offset);

		assert(byte_index < data_.size());

		if (value) {
			data_[byte_index] |= bitfield;
		} else {
			data_[byte_index] &= (~bitfield);
		}
	}

	bool operator[](std::size_t index) const {
		std::size_t byte_index{ index / 8 };
		std::size_t offset{ index % 8 };

		assert(byte_index < data_.size());
		int set{ (data_[byte_index] >> offset) & 0x1 };
		return set;
	}

	bool operator==(const DynamicBitset& other) const {
		return data_ == other.data_;
	}

	std::size_t Size() const {
		return bit_count_;
	}

	std::size_t Capacity() const {
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
	std::size_t GetByteCount(std::size_t bit_count) {
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
	impl::Index next_entity_{ 0 };
	impl::Index count_{ 0 };
	bool refresh_required_{ false };
	impl::DynamicBitset entities_;
	impl::DynamicBitset refresh_;
	std::vector<impl::Version> versions_;
	std::vector<std::shared_ptr<impl::PoolInterface>> pools_;
	std::deque<impl::Index> free_entities_;
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

	constexpr Manager([[maybe_unused]] bool initialize) {}

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

	Manager Clone() const {
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
			if (pool != nullptr) {
				clone.instance_->pools_[i] = pool->Clone();
			}
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
			impl::Index alive{ 0 };
			impl::Index dead{ 0 };
			for (impl::Index entity{ 0 }; entity < instance_->next_entity_; ++entity) {
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

	template <typename T>
	void ForEachEntity(T function);

	// Contemplate: Is there a way to do the following functions using
	// std::function? According to https://stackoverflow.com/a/5153276, there is
	// not.

	template <typename... Ts, typename T>
	void ForEachEntityWith(T function);
	template <typename... Ts, typename T>
	void ForEachEntityWithout(T function);

	template <typename... Ts>
	System<Ts...> EntitiesWith() {
		return System<Ts...>{ *this, instance_->next_entity_,
							  std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
	}

	std::size_t Size() const {
		return instance_->count_;
	}

	std::size_t Capacity() const {
		return instance_->versions_.capacity();
	}

	bool IsValid() const {
		return instance_ != nullptr;
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
	template <typename... Ts>
	friend class System;
	template <typename... Ts>
	friend class SystemIterator;
	friend class Entity;

	template <typename... Ts>
	void CopyEntity(
		impl::Index from_id, impl::Version from_version, impl::Index to_id, impl::Version to_version
	) {
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
			bool manager_has{
				((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools) != nullptr) && ...)
			};
			assert(
				manager_has && "Cannot copy entity with a component that is not "
							   "even in the manager"
			);
			bool entity_has{ (
				std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Has(from_id) &&
				...
			) };
			assert(entity_has && "Cannot copy entity with a component that it does not have");
			(std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Copy(
				 from_id, to_id
			 ),
			 ...);
		} else { // Copy all components.
			for (auto& pool : instance_->pools_) {
				if (pool != nullptr && pool->Has(from_id)) {
					pool->Copy(from_id, to_id);
				}
			}
		}
	}

	void GenerateEntity(impl::Index& entity, impl::Version& version) {
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
			instance_->versions_.resize(size, impl::null_version);
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

	void ClearEntity(impl::Index entity) {
		for (auto& pool : instance_->pools_) {
			if (pool != nullptr) {
				pool->Remove(entity);
			}
		}
	}

	bool IsAlive(impl::Index entity, impl::Version version) const {
		return version != impl::null_version && entity < instance_->versions_.size() &&
			   instance_->versions_[entity] == version && entity < instance_->entities_.Size() &&
			   // Entity considered currently alive or entity marked
			   // for creation/deletion but not yet created/deleted.
			   (instance_->entities_[entity] || instance_->refresh_[entity]);
	}

	bool IsActivated(impl::Index entity) const {
		assert(entity < instance_->entities_.Size());
		return instance_->entities_[entity];
	}

	impl::Version GetVersion(impl::Index entity) const {
		assert(entity < instance_->versions_.size());
		return instance_->versions_[entity];
	}

	bool Match(impl::Index entity1, impl::Index entity2) const {
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

	void DestroyEntity(impl::Index entity, impl::Version version) {
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
	const std::shared_ptr<impl::Pool<T>> GetPool(impl::Index component) const {
		assert(component == GetId<T>() && "GetPool mismatch with component id");
		if (component < instance_->pools_.size()) {
			const auto& pool = instance_->pools_[component];
			// This is nullptr if the pool does not exist in the manager.
			return std::shared_ptr<impl::Pool<T>>{ pool, static_cast<impl::Pool<T>*>(pool.get()) };
		}
		return nullptr;
	}

	template <typename T>
	std::shared_ptr<impl::Pool<T>> GetPool(impl::Index component) {
		assert(component == GetId<T>() && "GetPool mismatch with component id");
		if (component < instance_->pools_.size()) {
			auto& pool = instance_->pools_[component];
			// This is nullptr if the pool does not exist in the manager.
			return std::shared_ptr<impl::Pool<T>>{ pool, static_cast<impl::Pool<T>*>(pool.get()) };
		}
		return nullptr;
	}

	template <typename T>
	void Remove(impl::Index entity, impl::Index component) {
		auto pool{ GetPool<T>(component) };
		if (pool != nullptr) {
			pool->template Pool<T>::Remove(entity);
		}
	}

	template <typename... Ts>
	decltype(auto) Get(impl::Index entity) const {
		if constexpr (sizeof...(Ts) == 1) {
			const auto pool{ GetPool<Ts...>(GetId<Ts...>()) };
			assert(pool != nullptr && "Manager does not have the requested component");
			return pool->Get(entity);
		} else {
			const auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
			assert(
				((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools) != nullptr) && ...) &&
				"Manager does not have at least one of the requested components"
			);
			return std::forward_as_tuple<const Ts&...>((
				std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Get(entity)
			)...);
		}
	}

	template <typename... Ts>
	decltype(auto) Get(impl::Index entity) {
		if constexpr (sizeof...(Ts) == 1) {
			auto pool{ GetPool<Ts...>(GetId<Ts...>()) };
			assert(pool != nullptr && "Manager does not have the requested component");
			return pool->Get(entity);
		} else {
			auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
			assert(
				((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools) != nullptr) && ...) &&
				"Manager does not have at least one of the requested components"
			);
			return std::forward_as_tuple<Ts&...>((
				std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Get(entity)
			)...);
		}
	}

	template <typename T>
	bool Has(impl::Index entity, impl::Index component) const {
		const auto pool{ GetPool<T>(component) };
		return pool != nullptr && pool->Has(entity);
	}

	template <typename T, typename... Ts>
	T& Add(impl::Index entity, impl::Index component, Ts&&... constructor_args) {
		if (component >= instance_->pools_.size()) {
			instance_->pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
		}
		std::shared_ptr<impl::Pool<T>> pool{ GetPool<T>(component) };
		if (pool == nullptr) {
			pool						 = std::make_shared<impl::Pool<T>>();
			instance_->pools_[component] = pool;
		}
		assert(pool != nullptr && "Could not create new component pool correctly");
		return pool->Add(entity, std::forward<Ts>(constructor_args)...);
	}

	template <typename T>
	static impl::Index GetId() {
		// Get the next available id save that id as static variable for the
		// component type.
		static impl::Index id{ ComponentCount()++ };
		return id;
	}

	static impl::Index& ComponentCount() {
		static impl::Index id{ 0 };
		return id;
	}
	template <typename T>
	friend class impl::Pool;
	friend class Entity;

	std::shared_ptr<impl::ManagerInstance> instance_;
};

template <typename... Ts>
class SystemIterator {
public:
	using iterator_category = std::forward_iterator_tag;
	// TODO: Check that this is the case since entities do not have regular
	// distance
	using difference_type  = std::ptrdiff_t;
	using value_type	   = impl::Index;
	using dereference_type = std::tuple<Entity, Ts&...>;
	using access_type	   = value_type;

	dereference_type operator*() {
		assert(!IncorrectEntity() && "No entity with given components");
		assert(WithinEntityLimit() && "Out-of-range entity index");
		assert(!IsMaxEntity() && "Cannot dereference system iterator end");
		return GetComponentTuple();
	}

	access_type operator->() {
		assert(!IncorrectEntity() && "No entity with given components");
		assert(WithinEntityLimit() && "Out-of-range entity index");
		assert(!IsMaxEntity() && "Cannot dereference system iterator end");
		return entity_;
	}

	SystemIterator& operator++() {
		do {
			entity_++;
		} while (ShouldIncrement());
		return *this;
	}

	SystemIterator operator++(int) {
		SystemIterator tmp = *this;
		++(*this);
		return tmp;
	}

	friend bool operator==(const SystemIterator& a, const SystemIterator& b) {
		return a.entity_ == b.entity_;
	};

	friend bool operator!=(const SystemIterator& a, const SystemIterator& b) {
		return a.entity_ != b.entity_;
	};

private:
	SystemIterator(
		impl::Index entity, const Manager& manager, impl::Index max_entity,
		std::tuple<std::shared_ptr<impl::Pool<Ts>>...>& pools
	) :
		entity_(entity), manager_{ manager }, max_entity_{ max_entity }, pools_{ pools } {
		if (ShouldIncrement()) {
			this->operator++();
		}
		if (!IsMaxEntity()) {
			assert(
				WithinEntityLimit() &&
				"Cannot create system iterator with out-of-range entity index"
			);
		}
	}

	bool ShouldIncrement() const {
		return IncorrectEntity() && WithinEntityLimit();
	}

	bool IsMaxEntity() const {
		return entity_ == max_entity_;
	}

	bool WithinEntityLimit() const {
		return entity_ < max_entity_;
	}

	dereference_type GetComponentTuple() {
		using namespace impl;
		assert(manager_.IsValid() && "Cannot deference system with destroyed manager");
		assert(
			((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools_) != nullptr) && ...) &&
			"Component pools cannot be destroyed while looping through entities"
		);
		return dereference_type(
			Entity{ entity_, manager_.GetVersion(entity_), manager_ },
			(std::get<std::shared_ptr<impl::Pool<Ts>>>(pools_)->template Pool<Ts>::Get(entity_))...
		);
	}

	bool IncorrectEntity() const {
		using namespace impl;
		assert(manager_.IsValid() && "Manager cannot be destroyed while looping through entities");
		bool pool_is_null{
			((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools_) == nullptr) || ...)
		};
		if (pool_is_null) {
			return true;
		}
		bool activated{ manager_.IsActivated(entity_) };
		if (!activated) {
			return true;
		}
		bool missing_component{
			(!std::get<std::shared_ptr<impl::Pool<Ts>>>(pools_)->template Pool<Ts>::Has(entity_) ||
			 ...)
		};
		if (missing_component) {
			return true;
		}
		return false;
	}

private:
	template <typename... S>
	friend class System;

	impl::Index entity_{ 0 };
	Manager manager_{ false };
	impl::Index max_entity_{ 0 };
	std::tuple<std::shared_ptr<impl::Pool<Ts>>...>& pools_;
};

template <typename... Ts>
class System {
public:
	SystemIterator<Ts...> begin() {
		return SystemIterator<Ts...>(0, manager_, max_entity_, pools_);
	}

	SystemIterator<Ts...> end() {
		return SystemIterator<Ts...>(max_entity_, manager_, max_entity_, pools_);
	}

private:
	friend class Manager;

	System(
		const Manager& manager, impl::Index next_entity,
		std::tuple<std::shared_ptr<impl::Pool<Ts>>...>&& pools
	) :
		manager_{ manager }, max_entity_{ next_entity }, pools_{ pools } {}

	Manager manager_{ false };
	impl::Index max_entity_{ 0 };
	std::tuple<std::shared_ptr<impl::Pool<Ts>>...> pools_;
};

class Entity {
public:
	constexpr Entity() = default;
	~Entity()		   = default;

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
	bool Has() const {
		assert(IsAlive() && "Cannot check if dead or null entity has component(s)");
		return IsAlive() && (manager_.Has<Ts>(entity_, manager_.GetId<Ts>()) && ...);
	}

	template <typename... Ts>
	bool HasAny() const {
		assert(IsAlive() && "Cannot check if dead or null entity has any component(s)");
		return IsAlive() && (manager_.Has<Ts>(entity_, manager_.GetId<Ts>()) || ...);
	}

	template <typename... Ts>
	decltype(auto) Get() const {
		assert(IsAlive() && "Cannot get component(s) from dead or null entity");
		return manager_.Get<Ts...>(entity_);
	}

	template <typename... Ts>
	decltype(auto) Get() {
		assert(IsAlive() && "Cannot get component(s) from dead or null entity");
		return manager_.Get<Ts...>(entity_);
	}

	void Clear() {
		assert(IsAlive() && "Cannot clear components of dead or null entity");
		manager_.ClearEntity(entity_);
	}

	bool IsAlive() const {
		return manager_.IsValid() && manager_.IsAlive(entity_, version_);
	}

	void Destroy() {
		if (IsAlive()) {
			manager_.DestroyEntity(entity_, version_);
		}
	}

	Manager GetManager() {
		assert(manager_.IsValid() && "Cannot return parent manager of a null entity");
		return manager_;
	}

	bool IsIdenticalTo(const Entity& e) const;

private:
	friend class Manager;
	friend class impl::NullEntity;
	friend struct std::hash<Entity>;
	template <typename... Ts>
	friend class SystemIterator;

	Entity(impl::Index entity, impl::Version version, const Manager& manager) :
		entity_{ entity }, version_{ version }, manager_{ manager } {}

	impl::Index entity_{ 0 };
	impl::Version version_{ impl::null_version };
	Manager manager_{ false };
};

inline const Entity null;

template <typename T>
inline impl::Index impl::Pool<T>::GetId() const {
	return Manager::GetId<T>();
}

bool Entity::IsIdenticalTo(const Entity& e) const {
	return *this == e || (*this == ecs::null && e == ecs::null) ||
		   (*this != ecs::null && e != ecs::null && manager_ == e.manager_ && manager_.IsValid() &&
			entity_ != e.entity_ && manager_.Match(entity_, e.entity_));
}

inline Entity Manager::CreateEntity() {
	impl::Index entity{ 0 };
	impl::Version version{ impl::null_version };
	GenerateEntity(entity, version);
	assert(version != impl::null_version && "Failed to create new entity in manager");
	return { entity, version, *this };
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

template <typename T>
inline void Manager::ForEachEntity(T function) {
	assert(
		instance_->entities_.Size() == instance_->versions_.size() &&
		"Cannot loop through manager entities if and version and entity "
		"vectors differ in size"
	);
	assert(
		instance_->next_entity_ <= instance_->entities_.Size() &&
		"Last entity must be within entity vector range"
	);
	for (impl::Index entity{ 0 }; entity < instance_->next_entity_; ++entity) {
		if (instance_->entities_[entity]) {
			function(Entity{ entity, instance_->versions_[entity], *this });
		}
	}
}

template <typename... Ts, typename T>
inline void Manager::ForEachEntityWith(T function) {
	static_assert(
		sizeof...(Ts) > 0, "Cannot loop through each entity without providing "
						   "at least one component type"
	);
	assert(
		instance_->entities_.Size() == instance_->versions_.size() &&
		"Cannot loop through manager entities if and version and entity "
		"vectors differ in size"
	);
	assert(
		instance_->next_entity_ <= instance_->entities_.Size() &&
		"Last entity must be within entity vector range"
	);
	auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
	// Check that none of the requested component pools are nullptrs.
	if (((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools) != nullptr) && ...)) {
		for (impl::Index entity{ 0 }; entity < instance_->next_entity_; ++entity) {
			// If entity is alive and has the components, call lambda on it.
			if (instance_->entities_[entity] &&
				(std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Has(entity) &&
				 ...)) {
				function(
					Entity{ entity, instance_->versions_[entity], *this },
					(std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Get(entity
					))...
				);
			}
		}
	}
}

template <typename... Ts, typename T>
inline void Manager::ForEachEntityWithout(T function) {
	assert(
		instance_->entities_.Size() == instance_->versions_.size() &&
		"Cannot loop through manager entities if and version and entity "
		"vectors differ in size"
	);
	assert(
		instance_->next_entity_ <= instance_->entities_.Size() &&
		"Last entity must be within entity vector range"
	);
	auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
	// Check that none of the requested component pools are nullptrs.
	if (((std::get<std::shared_ptr<impl::Pool<Ts>>>(pools) != nullptr) && ...)) {
		for (impl::Index entity{ 0 }; entity < instance_->next_entity_; ++entity) {
			// If entity is alive and does not have one of the components, call
			// lambda on it.
			if (instance_->entities_[entity] &&
				(!std::get<std::shared_ptr<impl::Pool<Ts>>>(pools)->template Pool<Ts>::Has(entity
				 ) ||
				 ...)) {
				function(Entity{ entity, instance_->versions_[entity], *this });
			}
		}
	}
}

} // namespace ecs

namespace std {

template <>
struct hash<ecs::Entity> {
	std::size_t operator()(const ecs::Entity& e) const {
		// Source: https://stackoverflow.com/a/17017281
		std::size_t hash{ 17 };
		assert(e.manager_.IsValid() && "Cannot hash entity with manager that is nullptr");
		hash = hash * 31 + std::hash<const ecs::Manager*>()(&e.manager_);
		hash = hash * 31 + std::hash<ecs::impl::Index>()(e.entity_);
		hash = hash * 31 + std::hash<ecs::impl::Version>()(e.version_);
		return hash;
	}
};

} // namespace std
