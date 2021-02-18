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
#include <type_traits> // std::is_base_of_v, std::enable_if, etc
#include <cassert> // assert

namespace ecs {

namespace internal {

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
	// @return Pointer to an identical component pool.
	virtual BasePool* Clone() const = 0;
	// @return True if entity was successfully removed from the pool, false otherwise.
	virtual bool Remove(const Id entity) = 0;
};

template <typename TComponent>
class Pool : public BasePool {
public:
	Pool() {
		// Allocate enough memory for the first component.
		Allocate(1);
	}
	~Pool() {
		// Call destructor of all address with valid offsets.
		for (auto offset : offsets_) {
			if (offset) {
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
	Pool(Pool&& obj) noexcept : pool_{ obj.pool_ }, capacity_{ obj.capacity_ }, size_{ obj.size_ }, offsets_{ std::exchange(obj.offsets_, {}) }, available_offsets_{ std::exchange(obj.available_offsets_, {}) } {
		obj.pool_ = nullptr;
		obj.capacity_ = 0;
		obj.size_ = 0;
	}
	Pool& operator=(Pool&&) = delete;
	virtual BasePool* Clone() const {
		// Use memcpy or copy manually here.
		auto new_pool =  new Pool<TComponent>(sparse_set_, dense_set_, components_);
	}
	virtual void Remove(const Id entity) override final {
		
	}
private:
	Pool(TComponent* pool_,	std::size_t capacity_{ 0 };
	// Number of components currently in the pool.
	std::size_t size_{ 0 };
	// Sparse set which maps entity ids (index of element) 
	// to offsets from the start of the pool_ pointer.
	std::vector<Offset> offsets_;
	// List of free offsets (avoid reallocating entire pool block).
	// Choice of double ended queue as popping the front offset
	// allows for efficient component memory locality.
	std::deque<Offset> available_offsets_;) {}
	void Allocate(const std::size_t starting_capacity) {
		assert(pool_ == nullptr && size_ == 0 && capacity_ == 0 && "Cannot allocate memory for occupied component pool");
		capacity_ = starting_capacity;
		pool_ = static_cast<TComponent*>(std::malloc(capacity_ * sizeof(TComponent)));
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
}



} // namespace internal



} // namespace ecs