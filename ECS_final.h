/*

MIT License

Copyright (c) 2021 | Martin Starkov | https://github.com/martinstarkov

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

#include <cstdlib> // std::size_t, std::malloc, std::realloc, std::free
#include <vector> // std::vector
#include <deque> // std::deque
#include <tuple> // std::tuple
#include <type_traits> // std::enable_if, std::is_destructible_v, std::is_base_of_v, etc
#include <cassert> // assert

namespace ecs {

namespace internal {

namespace type_traits {

template <typename TComponent>
using is_valid_pool_t = std::enable_if_t<std::is_destructible_v<TComponent>, bool>;

template <typename TComponent, typename ...TArgs>
using is_valid_component_t = std::enable_if_t<std::is_constructible_v<TComponent, TArgs...>, bool>;

} // namespace type_traits

// Aliases.

using Id = std::uint32_t;
using Version = std::uint32_t;
using Offset = std::uint32_t;

// Constants.

// Represents an invalid version number.
constexpr Version null_version{ 0 };

class BasePool {
public:
	virtual ~BasePool() = default;
	virtual BasePool* Clone() const = 0;
	virtual bool Remove(const Id entity) = 0;
};

template <typename TComponent, type_traits::is_valid_pool_t<TComponent> = true>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		Allocate(1);
	}
	~Pool() {
		// Call destructor of all addresses with valid offsets.
		for (auto offset : offsets_) {
			if (offset != 0) {
				auto address = pool_ + (offset - 1);
				address->~TComponent();
			}
		}
		assert(pool_ != nullptr && "Cannot free invalid component pool pointer");
		// Free the allocated pool memory block.
		std::free(pool_);
	}
	// Component pools should never be copied.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;
	// Move operator used for resizing a vector of component pools (in the manager class).
	Pool(Pool&& obj) noexcept : 
		pool_{ obj.pool_ }, 
		capacity_{ obj.capacity_ }, 
		size_{ obj.size_ }, 
		offsets_{ std::exchange(obj.offsets_, {}) }, 
		available_offsets_{ std::exchange(obj.available_offsets_, {}) } {
		obj.pool_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	Pool& operator=(Pool&&) = delete;
	/*
	* @return Pointer to an identical component pool.
	*/
	virtual BasePool* Clone() const override final {
		TComponent* new_block = nullptr;
		// Copy entire pool block over to new pool.
		std::memcpy(new_block, pool_, capacity_ * sizeof(TComponent));
		return new Pool<TComponent>(new_block, capacity_, size_, offsets_, available_offsets_);
	}
	/* 
	* @param Id of the entity to remove a component for.
	*/
	virtual void Remove(const Id entity) override final {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != 0) {
				auto address = pool_ + (offset - 1);
				// Call destructor on component memory location.
				address->~TComponent();
				available_offsets_.emplace_back(offset);
				// Set offset to invalid.
				offset = 0;
			}
		}
	}
	/* 
	* Create / replace a component in the pool.
	* @param Id of the entity for the added component.
	* @param Arguments to be passed to the component constructor.
	* @return Pointer to the newly added / replaced component
	*/
	template <typename ...TArgs, type_traits::is_valid_component_t<TComponent, TArgs...> = true>
	TComponent* Add(const Id entity, TArgs&&... constructor_args) {
		auto offset = GetAvailableOffset();
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		if (entity >= offsets_.size()) {
			offsets_.resize(entity + 1, 0);
		}
		offsets_[entity] = offset;
		auto address = pool_ + (offset - 1);
		// Call destructor on potential previous components
		// at the address.
		address->~TComponent();
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		return address;
	}
	/*
	* @param Id of the entity to check a component for.
	* @return True if the pool contains a valid offset, false otherwise.
	*/
	bool Has(const Id entity) const {
		return entity < offsets_.size() && offsets_[entity] != 0;
	}
	/*
	* @param Id of the entity to retrieve a component for.
	* @return The memory location of a component, nullptr if it does not exist.
	*/
	TComponent* Get(const Id entity) {
		if (Has(entity)) {
			return pool_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}
private:
	// Constructor used for cloning pools.
	Pool(const TComponent* pool, 
		 const std::size_t capacity, 
		 const std::size_t size, 
		 const std::vector<Offset>& offsets, 
		 const std::deque<Offset>& available_offsets) : 
		pool_{ pool }, 
		capacity_{ capacity }, 
		size_{ size }, 
		offsets_{ offsets }, 
		available_offsets_{ available_offsets } {}
	/*
	* Allocate some initial amount of memory for the pool.
	* @param The starting capacity of the pool (# of components it will support to begin with).
	*/
	void Allocate(const std::size_t starting_capacity) {
		assert(pool_ == nullptr && size_ == 0 && capacity_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		pool_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
	}
	/*
	* Double the capacity of a pool if the current capacity is exceeded.
	* @param New desired size of the pool (minimum # of components it should support).
	*/
	void ReallocateIfNeeded(const std::size_t new_size) {
		if (new_size >= capacity_) {
			// Double the capacity.
			capacity_ = new_size * 2;
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			pool_ = static_cast<TComponent*>(std::realloc(pool_, capacity_ * sizeof(TComponent)));
		}
	}
	/* 
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset can be generated.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset next_offset{ 0 };
		if (available_offsets_.size() > 0) {
			// Take offset from the front of the free offsets.
			// This better preserves component locality as
			// components are pooled (pun) in the front.
			next_offset = available_offsets_.front();
			available_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			next_offset = size_++;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_offset != 0 && "Could not find a valid offset from component pool");
		return next_offset;
	}
	// Pointer to the beginning of the pool's memory block.
	TComponent* pool_{ nullptr };
	// Component capacity of the pool.
	std::size_t capacity_{ 0 };
	// Number of components currently in the pool.
	std::size_t size_{ 0 };
	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the pool_ pointer.
	std::vector<Offset> offsets_;
	// List of free offsets (avoid reallocating entire pool block).
	// Choice of double ended queue as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> available_offsets_;
};

} // namespace internal

// Forward declarations.

class Entity;

class Manager {
public:
	Manager() = default;
	~Manager() {
		// Destroy component pools.
		for (auto pool : pools_) {
			delete pool;
		}
	}
	// Managers cannot be copied. Use Clone() if you wish 
	// to create a new manager with identical composition.
	Manager& operator=(const Manager&) = delete;
	Manager(const Manager&) = delete;
	Manager(Manager&& obj) noexcept :
		next_entity_{ obj.next_entity_ },
		entities_{ std::exchange(obj.entities_, {}) },
		refresh_{ std::exchange(obj.refresh_, {}) },
		versions_{ std::exchange(obj.versions_, {}) },
		pools_{ std::exchange(obj.pools_, {}) },
		free_entities_{ std::exchange(obj.free_entities_, {}) } {
		obj.next_entity_ = 0;
	}
	Manager& operator=(Manager&& obj) noexcept {
		// Deallocate previous manager pools.
		for (auto pool : pools_) {
			delete pool;
		}
		// Move manager into current manager.
		next_entity_ = obj.next_entity_;
		entities_ = std::exchange(obj.entities_, {});
		refresh_ = std::exchange(obj.refresh_, {});
		versions_ = std::exchange(obj.versions_, {});
		pools_ = std::exchange(obj.pools_, {});
		free_entities_ = std::exchange(obj.free_entities_, {});
		// Reset state of other manager.
		obj.next_entity_ = 0;
	}
	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use this in performance critical code.
	* @param Manager to compare with.
	* @return True if manager composition is identical, false otherwise.
	*/
	bool operator==(const Manager& other) const {
		return next_entity_ == other.next_entity_ 
			&& entities_ == other.entities_ 
			&& versions_ == other.versions_
			&& pools_ == other.pools_
			&& free_entities_ == other.free_entities_
			&& refresh_ == other.refresh_;
	}
	/*
	* Note that managers are not unique (can be cloned).
	* It is not advisable to use this in performance critical code.
	* @param Manager to compare with.
	* @return True if manager composition differs, false otherwise.
	*/
	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}
	/*
	* Copying managers accidentally is expensive.
	* This provides a way of replicating a manager with
	* identical entities and components.
	* @return New manager with identical composition.
	*/
	Manager Clone() const {
		Manager clone;
		// id_ already set to next available one inside default constructor.
		clone.next_entity_ = next_entity_;
		clone.entities_ = entities_;
		clone.refresh_ = refresh_;
		clone.versions_ = versions_;
		clone.free_entities_ = free_entities_;
		clone.pools_.resize(pools_.size(), nullptr);
		for (std::size_t i = 0; i < pools_.size(); ++i) {
			auto pool = pools_[i];
			if (pool != nullptr) {
				clone.pools_[i] = pool->Clone();
			}
		}
		assert(clone == *this && "Cloning manager failed");
		return clone;
	}

	void Clear() {
		next_entity_ = 0;
		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();
	}

	void Reset() {
		next_entity_ = 0;
		entities_.clear();
		refresh_.clear();
		versions_.clear();
		free_entities_.clear();
		for (auto pool : pools_) {
			delete pool;
		}
	}

	// TODO: Add create entity function.

private:
	// Entity handles must have access to internal functions.
	// This because ids are internal and (mostly) hidden from the user.
	friend class Entity;
	// Stores the next valid entity id.
	// This will be incremented if no free id is found.
	internal::Id next_entity_{ 0 };
	// Vector index corresponds to the entity's id.
	// Element corresponds to whether or not the entity 
	// is currently alive.
	std::vector<bool> entities_;
	// Vector index corresponds to the entity's id.
	// Element corresponds to a flag for refreshing the entity.
	std::vector<bool> refresh_;
	// Vector index corresponds to the entity's id.
	// Element corresponds to the current version of the id.
	std::vector<internal::Version> versions_;
	// Vector index corresponds to the component's unique id.
	// If a component has not been added to a manager entity, 
	// its corresponding pool will be nullptr.
	std::vector<internal::BasePool*> pools_;
	// Free list of entity ids to be used 
	// before incrementing next_entity_.
	std::deque<internal::Id> free_entities_;
};




} // namespace ecs