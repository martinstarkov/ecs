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
#include <cstdint> // std::uint32_t
#include <vector> // std::vector
#include <array> // std::array
#include <deque> // std::deque
#include <tuple> // std::tuple
#include <functional> // std::hash
#include <utility> // std::exchange
#include <algorithm> // std::max_element
#include <type_traits> // std::enable_if_t, std::is_destructible_v, std::is_base_of_v, etc
#include <cassert> // assert

namespace ecs {

// Forward declarations for user-accessible types.

class Entity;
class NullEntity;
class Manager;
template <typename ...TComponents>
class System;

namespace internal {

// Forward declarations for internally used types.

class BasePool;
class BaseSystem;

// Aliases.

using Id = std::uint32_t;
using Version = std::uint32_t;
// Type representing the offset of a component 
// from the start of a memory block in a pool.
using Offset = std::uint32_t;

namespace type_traits {

template <typename TComponent>
inline constexpr bool is_valid_component_v{ 
	   std::is_destructible_v<TComponent>
	&& std::is_move_constructible_v<TComponent>
};

template <typename TComponent>
using is_valid_component = std::enable_if_t<
	is_valid_component_v<TComponent>,
	bool>;

template <typename T, typename ...TArgs>
using is_constructible = std::enable_if_t<std::is_constructible_v<T, TArgs...>, bool>;

} // namespace type_traits

class BasePool {
public:
	virtual BasePool* Clone() const = 0;
	virtual void Clear() = 0;
	virtual bool Remove(const Id entity) = 0;
	virtual std::size_t Hash() const = 0;
};

class BaseSystem {
public:

protected:

private:

};

template <typename TComponent, 
	type_traits::is_valid_component<TComponent> = true>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		// This enables capacity to double in powers of 2.
		AllocateMemoryBlock(1);
	}

	~Pool() {
		CallComponentDestructors();
		FreeMemoryBlock();
		assert(block_ == nullptr && "Pool memory must be freed before pool destruction");
	}

	// Component pools should never be copied.
	// Use Clone() instead.
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	// Move operator used when resizing a vector 
	// of component pools (in the manager class).
	Pool(Pool&& obj) noexcept :
		block_{ obj.block_ },
		capacity_{ obj.capacity_ },
		size_{ obj.size_ },
		offsets_{ std::exchange(obj.offsets_, {}) },
		freed_offsets_{ std::exchange(obj.freed_offsets_, {}) } {
		obj.block_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	
	// Pools should never be move assigned.
	Pool& operator=(Pool&&) = delete;
	
	/*
	* Creates a duplicate (copy) pool with
	* all the same components and offsets.
	* @return Pointer to an identical component pool.
	*/
	virtual BasePool* Clone() const override final {
		static_assert(std::is_copy_constructible_v<TComponent>, 
					  "Cannot clone pool with a non copy constructible component");
		// Empty memory block for clone is allocated in constructor.
		auto clone = new Pool<TComponent>(
			capacity_, 
			size_, 
			offsets_, 
			freed_offsets_
		);
		// Copy entire pool block over to new pool block.
		for (auto offset : offsets_) {
			if (offset != 0) {
				TComponent* component = block_ + (offset - 1);
				TComponent* address = clone->block_ + (offset - 1);
				// Copy component from current pool to 
				// other pool using copy constructor.
				new(address) TComponent{ *component };
			}
		}
		return clone;
	}

	// Calls destructor on each component.
	// Clears free and occupied offsets.
	// Does not modify pool capacity.
	// Equivalent to clearing a vector.
	virtual void Clear() override final {
		CallComponentDestructors();
		offsets_.clear();
		freed_offsets_.clear();
		size_ = 0;
	}

	/*
	* Removes the component from the given entity.
	* @param entity Id of the entity to remove a component from.
	* @return True if component was removed, false otherwise.
	*/
	virtual bool Remove(const Id entity) override final {
		if (entity < offsets_.size()) {
			auto& offset = offsets_[entity];
			if (offset != 0) {
				TComponent* address = block_ + (offset - 1);
				// Call destructor on component memory location.
				address->~TComponent();
				freed_offsets_.emplace_back(offset);
				// Set offset to invalid.
				offset = 0;
				return true;
			}
		}
		return false;
	}

	/*
	* Creates / replaces a component for an entity in the pool.
	* Component must be constructible from the given arguments.
	* @tparam TArgs Types of constructor arguments.
	* @param entity Id of the entity to add a component to.
	* @param constructor_args Arguments to be passed to the component constructor.
	* @return Reference to the newly added / replaced component.
	*/
	template <typename ...TArgs, 
		type_traits::is_constructible<TComponent, TArgs...> = true>
	TComponent& Add(const Id entity, TArgs&&... constructor_args) {
		auto offset = GetAvailableOffset();
		// If the entity exceeds the indexing table's size, 
		// expand the indexing table with invalid offsets.
		if (static_cast<std::size_t>(entity) >= offsets_.size()) {
			offsets_.resize(static_cast<std::size_t>(entity) + 1, 0);
		}
		// Check if component offset exists already.
		bool replace = offsets_[entity] != 0;
		offsets_[entity] = offset;
		TComponent* address = block_ + (offset - 1);
		if (replace) {
			// Call destructor on potential previous components
			// at the address.
			address->~TComponent();
		}
		// Create the component into the new address with
		// the given constructor arguments.
		new(address) TComponent(std::forward<TArgs>(constructor_args)...);
		assert(address != nullptr && "Failed to create component at offset memory location");
		return *address;
	}

	/*
	* Checks if the entity has a component in the pool.
	* @param entity Id of the entity to check a component for.
	* @return True if the pool contains a valid component offset, false otherwise.
	*/
	bool Has(const Id entity) const {
		return entity < offsets_.size() && offsets_[entity] != 0;
	}

	/*
	* Retrieves the component of a given entity from the pool.
	* @param entity Id of the entity to retrieve a component for.
	* @return Pointer to the component, nullptr if the component does not exist.
	*/
	TComponent* Get(const Id entity) {
		if (Has(entity)) {
			return block_ + (offsets_[entity] - 1);
		}
		return nullptr;
	}

	/*
	* Generates a hash number using pool members.
	* Useful for identifying if two pools are identical.
	* @return Hash code for the pool.
	*/
	virtual std::size_t Hash() const override final {
		// Hashing combination algorithm from:
		// https://stackoverflow.com/a/17017281
		std::size_t h = 17;
		h = h * 31 + std::hash<Offset>()(size_);
		h = h * 31 + std::hash<Offset>()(capacity_);
		// Modified container hashing from:
		// https://stackoverflow.com/a/27216842
		auto container_hash = [](auto& v) {
			std::size_t seed = v.size();
			for (auto i : v) {
				seed ^= static_cast<std::size_t>(i) 
					+ 0x9e3779b9 
					+ (seed << 6) 
					+ (seed >> 2);
			}
			return seed;
		};
		h = h * 31 + container_hash(offsets_);
		h = h * 31 + container_hash(freed_offsets_);
		return h;
	}

private:
	// Constructor used for cloning identical pools.
	Pool(const Offset capacity,
		 const Offset size,
		 const std::vector<Offset>& offsets,
		 const std::deque<Offset>& freed_offsets) : 
		// Allocate memory block before capacity is set 
		// as otherwise capacity == 0 assertion fails.
		capacity_{ (AllocateMemoryBlock(capacity), capacity) },
		size_{ size },
		offsets_{ offsets },
		freed_offsets_{ freed_offsets } {
	}

	/*
	* Allocate an initial memory block for the pool.
	* @param starting_capacity The starting capacity of the pool.
	* (number of components it should support to begin with).
	*/
	void AllocateMemoryBlock(const Offset starting_capacity) {
		assert(block_ == nullptr);
		assert(capacity_ == 0);
		assert(size_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		block_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
		assert(block_ != nullptr && "Could not properly allocate memory for component pool");
	}

	// Invokes the destructor of each valid component in the pool.
	// Note: valid offsets are not refreshed afterward.
	void CallComponentDestructors() {
		for (auto offset : offsets_) {
			// Only consider valid offsets.
			if (offset != 0) {
				TComponent* address = block_ + (offset - 1);
				address->~TComponent();
			}
		}
	}

	// Frees the allocated memory block associated with the pool.
	// This must be called whenever destroying a pool.
	void FreeMemoryBlock() {
		assert(block_ != nullptr && "Cannot free invalid component pool pointer");
		std::free(block_);
		block_ = nullptr;
	}

	/*
	* Doubles the capacity of a pool if the current capacity is exceeded.
	* @param new_size Desired size of the pool.
	* (minimum number of components it should support).
	*/
	void ReallocateIfNeeded(const Offset new_size) {
		if (new_size >= capacity_) {
			// Double the capacity.
			capacity_ = new_size * 2;
			assert(block_ != nullptr && "Pool memory must be allocated before reallocation");
			block_ = static_cast<TComponent*>(std::realloc(block_, capacity_ * sizeof(TComponent)));
		}
	}
	
	/*
	* Checks available offset list before generating a new offset.
	* Reallocates the pool if no new offset is available.
	* @return The first available (unused) offset in the component pool.
	*/
	Offset GetAvailableOffset() {
		Offset next_offset{ 0 };
		if (freed_offsets_.size() > 0) {
			// Take offset from the front of the free offsets.
			// This better preserves component locality as
			// components are pooled (pun) in the front.
			next_offset = freed_offsets_.front();
			freed_offsets_.pop_front();
		} else {
			// 'Generate' new offset at the end of the pool.
			// Offset of 0 is considered invalid.
			// In each access case 1 is subtracted so 0th
			// offset is still used.
			next_offset = ++size_;
			// Expand pool if necessary.
			ReallocateIfNeeded(size_);
		}
		assert(next_offset != 0 && "Could not find a valid offset from component pool");
		return next_offset;
	}

	// Pointer to the beginning of the pool's memory block.
	TComponent* block_{ nullptr };

	// Component capacity of the pool.
	Offset capacity_{ 0 };

	// Number of components currently in the pool.
	Offset size_{ 0 };

	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the block_ pointer.
	std::vector<Offset> offsets_;

	// Queue of free offsets (avoids frequent reallocation of pools).
	// Double ended queue is chosen as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> freed_offsets_;
};

} // namespace internal

class Entity {
public:

private:

};

template <typename ...TRequiredComponents>
class System {
public:

protected:

private:

};

class Manager {
public:

private:

};



} // namespace ecs
