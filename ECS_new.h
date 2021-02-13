#pragma once

#include <vector> // std::vector
#include <deque> // std::deque
#include <cstdlib> // std::size_t
#include <cstdint> // std::uint64_t, std::uint32_t
#include <memory> // std::unique_ptr, std::make_unique
#include <cassert> // assert

// TODO: TEMPORARY: ONLY FOR TESTS
class ManagerBasics;
class EntityBasics;

namespace ecs {

namespace internal {

using Index = std::uint64_t;
using NIndex = std::int64_t;
using Version = std::uint32_t;

constexpr Version null_version = 0;

class BasePool {
public:
	virtual std::unique_ptr<BasePool> Clone() const = 0;
	virtual void Destroy() = 0;
	virtual void VirtualRemove(Index entity) = 0;
};

template <typename Component>
class Pool : public BasePool {
private:
	constexpr static NIndex INVALID_INDEX = -1;
public:
	Pool() = default;
	~Pool() {
		Destroy();
	}
	Pool(const Pool&) = default;
	Pool& operator=(const Pool&) = default;
	Pool(Pool&&) = default;
	Pool& operator=(Pool&&) = default;
	virtual std::unique_ptr<BasePool> Clone() const override final {
		return std::make_unique<BasePool>(sparse_set_, dense_set_);
	}
	virtual void Destroy() override final {
		dense_set_.clear();
		sparse_set_.clear();
	}
	virtual void VirtualRemove(Index entity) override final {
		Remove(entity);
	}
	void Remove(Index entity) {
		if (entity < sparse_set_.size()) {
			auto& index = sparse_set_[entity];
			if (index < dense_set_.size() && index != INVALID_INDEX) {
				if (dense_set_.size() > 1) {
					if (entity == sparse_set_.size() - 1) {
						index = INVALID_INDEX;
						auto first_valid_entity = std::find_if(sparse_set_.rbegin(), sparse_set_.rend(), [](NIndex index) { return index != INVALID_INDEX; });
						sparse_set_.erase(first_valid_entity.base(), sparse_set_.end());
						sparse_set_.shrink_to_fit();
					} else {
						std::iter_swap(dense_set_.begin() + index, dense_set_.end() - 1);
						sparse_set_.back() = index;
						index = INVALID_INDEX;
					}
					dense_set_.pop_back();
				} else {
					sparse_set_.clear();
					dense_set_.clear();
				}
			}
		}
	}
private:
	Pool(const std::vector<NIndex>& sparse_set, const std::vector<Component>& dense_set) : sparse_set_{ sparse_set }, dense_set_{ dense_set } {}
	std::vector<NIndex> sparse_set_;
	std::vector<Component> dense_set_;
};

struct EntityData {
	EntityData() = default;
	~EntityData() = default;
	EntityData(const EntityData&) = default;
	EntityData& operator=(const EntityData&) = default;
	EntityData(EntityData&&) = default;
	EntityData& operator=(EntityData&&) = default;
	bool operator==(const EntityData& other) const {
		return index == other.index && counter == other.counter && alive == other.alive;
	}
	bool operator!=(const EntityData& other) const {
		return !operator==(other);
	}

	bool alive{ false };
	Index index{ 0 };
	Version counter{ null_version };
};

class BaseSystem {
public:
	virtual std::unique_ptr<internal::BaseSystem> Clone() const = 0;
};

} // namespace internal

struct Entity;

class Manager {
public:
	Manager() = default;
	~Manager() = default;
	Manager(Manager&&) = default;
	Manager& operator=(Manager&&) = default;
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	bool operator==(const Manager& other) const { 
		return size_ == other.size_ && size_next_ == other.size_next_ && entities_ == other.entities_ && component_pools_ == other.component_pools_;
	}
	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}
	Entity CreateEntity();

	// Credit for the refresh algorithm goes to Vittorio Romeo for his talk on entity component systems at CppCon 2015.
	internal::Index RefreshImpl() {
		internal::Index dead{ 0 };
		internal::Index alive{ size_next_ - 1 };

		while (true) {
			for (; true; ++dead) {
				if (dead > alive) return dead;
				if (!entities_[dead].alive) break;
			}
			for (; true; --alive) {
				if (entities_[alive].alive) break;
				++entities_[alive].counter;
				RemoveComponents(alive);
				if (alive <= dead) return dead;
			}
			assert(entities_[alive].alive);
			assert(!entities_[dead].alive);
			std::swap(entities_[alive], entities_[dead]);
			++entities_[alive].counter;
			RemoveComponents(alive);
			++dead; --alive;
		}

		return dead;
	}
	void Refresh() {
		if (size_next_ == 0) {
			size_ = 0;
			return;
		}
		size_ = size_next_ = RefreshImpl();
	}

	std::size_t GetEntityCount() {
		return size_;
	}
	/*
	std::size_t GetDeadEntityCount() {
		return dead_list_.size();
	}*/

private:
	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class ManagerBasics;
	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class EntityBasics;

	friend struct Entity;

	void DestroyEntity(internal::Index entity, internal::Version counter) {
		if (entity < entities_.size() && entities_[entity].counter == counter && entities_[entity].alive) {
			entities_[entity].alive = false;
		}
	}

	void RemoveComponents(internal::Index entity) {
		for (auto& pool : component_pools_) {
			pool->VirtualRemove(entity);
		}
	}

	std::size_t size_{ 0 };
	std::size_t size_next_{ 0 };
	std::vector<internal::EntityData> entities_;
	std::vector<std::unique_ptr<internal::BasePool>> component_pools_;
	std::vector<std::unique_ptr<internal::BaseSystem>> systems_;
	static internal::Index& GetComponentCount() { static internal::Index id{ 0 }; return id; }
	static internal::Index& GetSystemCount() { static internal::Index id{ 0 }; return id; }
};

struct Entity {
	Entity() = default;
	~Entity() = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	bool operator==(const Entity& other) const {
		return handle_ == other.handle_ && counter_ == other.counter_ && manager_ == other.manager_;
	}
	bool operator!=(const Entity& other) const {
		return !operator==(other);
	}
	void Destroy() {
		if (manager_ != nullptr) {
			manager_->DestroyEntity(handle_, counter_);
		}
	}
private:

	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class EntityBasics;

	friend class Manager;
	friend struct NullEntity;
	Entity(Manager* manager, internal::Index handle, internal::Version counter) : manager_{ manager }, handle_{ handle }, counter_{ counter } {}
	Manager* const manager_{ nullptr };
	const internal::Index handle_{ 0 };
	internal::Version counter_{ internal::null_version };
};

struct NullEntity {
	operator Entity() const {
		return Entity{ nullptr, 0, internal::null_version };
	}
	constexpr bool operator==(const NullEntity&) const {
		return true;
	}
	constexpr bool operator!=(const NullEntity&) const {
		return false;
	}
	bool operator==(const Entity& entity) const {
		return entity.counter_ == internal::null_version;
	}
	bool operator!=(const Entity& entity) const {
		return !(*this == entity);
	}
};

bool operator==(const Entity& entity, const NullEntity& null_entity) {
	return null_entity == entity;
}
bool operator!=(const Entity& entity, const NullEntity& null_entity) {
	return !(null_entity == entity);
}

inline constexpr NullEntity null{};

inline Entity Manager::CreateEntity() {
	auto capacity{ entities_.capacity() };
	if (size_next_ >= capacity) {
		auto new_capacity{ (capacity + 10) * 2 };
		entities_.resize(new_capacity);
		for (auto i{ capacity }; i < new_capacity; ++i) {
			entities_[i].index = i;
		}
	}
	internal::Index free_index{ size_next_++ };
	auto& entity{ entities_[free_index] };
	assert(!entity.alive);
	entity.alive = true;
	return Entity{ this, entity.index, entity.counter };
}

} // namespace ecs