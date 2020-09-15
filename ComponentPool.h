#pragma once

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>

using EntityId = std::int64_t;
using Offset = std::int64_t;

constexpr Offset INVALID_OFFSET = -1;
constexpr std::int16_t RECOMPACT_THRESHOLD = 5;

template<typename T>
void DestroyComponent(void* component_address) {
	static_cast<T*>(component_address)->~T();
}

using Destructor = void (*)(void*);

class ComponentPool {
public:
	ComponentPool() = delete;
	ComponentPool(std::size_t component_size, Destructor destructor) : component_size_{ component_size }, destructor_{ destructor } {
		AllocatePool(2);
	}
	// Free memory block and call destructors on everything
	~ComponentPool() = delete;
	// Figure these out later
	ComponentPool(const ComponentPool&) = delete;
	ComponentPool& operator=(const ComponentPool&) = delete;
	ComponentPool(ComponentPool&&) = delete;
	ComponentPool& operator=(ComponentPool&&) = delete;
	void* AddComponentAddress(EntityId id) {
		Offset component = AddToPool(id);
		AddOffset(id, component);
		return static_cast<void*>(pool_ + component);
	}
	void RemoveComponentAddress(EntityId id) {
		if (id < component_offsets_.size()) {
			Offset& component = component_offsets_[id];
			if (component != INVALID_OFFSET) {
				// remove address and free memory
				void* component_address = static_cast<void*>(pool_ + component);
				destructor_(component_address);
				std::memset(component_address, 0, component_size_);
				free_offsets_.emplace_back(component);
				component = INVALID_OFFSET;
			}
		}
	}
	void* GetComponentAddress(EntityId id) {
		if (id < component_offsets_.size()) {
			Offset component = component_offsets_[id];
			if (component != INVALID_OFFSET) {
				return static_cast<void*>(pool_ + component);
			}
		}
		return nullptr;
	}
private:
	void AllocatePool(std::size_t starting_capacity) {
		assert(size_ == 0 && capacity_ == 0 && pool_ == nullptr && "Cannot call initial pool allocation on occupied pool");
		capacity_ = starting_capacity;
		void* memory = std::malloc(capacity_);
		assert(memory != nullptr && "Failed to allocate initial memory for pool");
		pool_ = static_cast<char*>(memory);
	}
	void ReallocatePool(std::size_t new_capacity) {
		if (new_capacity >= capacity_) {
			assert(pool_ != nullptr && "Pool memory must be allocated before reallocation");
			capacity_ = new_capacity * 2; // double pool capacity when cap is reached
			auto memory = std::realloc(pool_, capacity_);
			assert(memory != nullptr && "Failed to reallocate memory for pool");
			pool_ = static_cast<char*>(memory);
		}
	}
	//void RecompactPool() {
	//	// Sort free memory by descending order of offsets so memory shifting can happen from end to beginning
	//	std::sort(free_offsets_.begin(), free_offsets_.end(), [](const Offset lhs, const Offset rhs) {
	//		return lhs > rhs;
	//	});
	//	// calculate how much memory would be removed from empty
	//	std::size_t free_components = free_offsets_.size();
	//	static_assert(RECOMPACT_THRESHOLD > 0, "Cannot recompact pool with no free components");
	//	if (free_components >= RECOMPACT_THRESHOLD) {
	//		size_ -= free_components * component_size_;
	//		assert(size_ >= 0 && "Cannot shrink pool size below 0");
	//		RecompactPool();
	//	}
	//	for (auto i = 0; i < free_offsets_.size(); ++i) {
	//		Offset free_offset = free_offsets_[i];
	//		//std::memcpy();
	//	}
	//}
	Offset AddToPool(EntityId id) {
		Offset next;
		if (free_offsets_.size() > 0) {
			auto it = free_offsets_.begin();
			next = *it;
			free_offsets_.erase(it);
		} else {
			next = size_;
			size_ += component_size_;
			ReallocatePool(size_);
		}
		return next;
	}
	void AddOffset(EntityId id, Offset component) {
		if (id >= component_offsets_.size()) {
			std::size_t new_size = id + 1;
			component_offsets_.resize(new_size, INVALID_OFFSET);
		}
		component_offsets_[id] = component;
	}
	char* pool_{ nullptr };
	Destructor destructor_;
	std::size_t capacity_{ 0 };
	std::size_t size_{ 0 };
	std::size_t component_size_;
	// Index is EntityId, element if the corresponding component's offset from the start of data_
	std::vector<Offset> component_offsets_;
	std::vector<Offset> free_offsets_;
};