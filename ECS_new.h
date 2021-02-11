#pragma once

#include <vector> // std::vector
#include <cstdlib> // std::size_t
#include <cstdint> // std::uint64_t, std::uint32_t
#include <memory> // std::unique_ptr, std::make_unique
#include <cassert> // assert

// TODO: TEMPORARY: ONLY FOR TESTS
class ManagerBasics;
class EntityBasics;

namespace ecs {

namespace internal {

using Index = std::int64_t;
using Id = std::uint64_t;
using Version = std::uint32_t;

constexpr Version null_version = 0;

class BasePool {
public:
	virtual std::unique_ptr<BasePool> Clone() const = 0;
	virtual void Destroy() = 0;
	virtual void VirtualRemove(Id entity) = 0;
};

template <typename Component>
class Pool : public BasePool {
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
	virtual void VirtualRemove(Id entity) override final {
		Remove(entity);
	}
	void Remove(Id entity) {
		if (entity < sparse_set_.size()) {
			auto& index = sparse_set_[entity];
			if (index < dense_set_.size() && index != INVALID_INDEX) {
				if (dense_set_.size() > 1) {
					if (entity == sparse_set_.size() - 1) {
						index = INVALID_INDEX;
						auto first_valid_entity = std::find_if(sparse_set_.rbegin(), sparse_set_.rend(), [](Index index) { return index != INVALID_INDEX; });
						sparse_set_.erase(first_valid_entity.base(), sparse_set_.end());
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
	constexpr static Index INVALID_INDEX = -1;
	Pool(const std::vector<Index>& sparse_set, const std::vector<Component>& dense_set) : sparse_set_{ sparse_set }, dense_set_{ dense_set } {}
	std::vector<Index> sparse_set_;
	std::vector<Component> dense_set_;
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
		return entities_ == other.entities_ && component_pools_ == other.component_pools_;
	}
	bool operator!=(const Manager& other) const {
		return !operator==(other);
	}
	Manager Clone() const {
		ecs::Manager clone;
		clone.entities_ = entities_;
		clone.component_pools_.reserve(component_pools_.size());
		for (auto& pool : component_pools_) {
			clone.component_pools_.emplace_back(std::move(pool->Clone()));
		}
		clone.systems_.reserve(systems_.size());
		for (auto& pool : systems_) {
			clone.systems_.emplace_back(std::move(pool->Clone()));
		}
		return clone;
	}
	std::size_t GetEntityCount() {
		return size_;
	}
	Entity CreateEntity();
	void Refresh() {
		if (next_size_ == 0) {
			size_ = 0;
			entities_.clear();
			return;
		}
		size_ = next_size_ = RefreshImplementation();
	}
private:
	void Destroy(internal::Id entity) {
		if (entity < entities_.size()) {
			entities_[entity] = internal::null_version;
		}
	}
	void GrowEntitiesIfNeeded() {
		if (entities_.capacity() > next_size_) return;
		entities_.resize(entities_.capacity() * 2, internal::null_version);
	}
	void RemoveComponents(internal::Id entity) {
		for (auto& pool : component_pools_) {
			pool->VirtualRemove(entity);
		}
	}
	internal::Id RefreshImplementation() {
		internal::Id dead{ 0 };
		internal::Id alive{ next_size_ - 1 };

		while (true) {
			for (; true; ++dead) {
				if (dead > alive) return dead;
				if (entities_[dead] == internal::null_version) break;
			}

			for (; true; --alive) {
				if (entities_[alive] != internal::null_version) break;
				RemoveComponents(alive);
				++entities_[alive];
				if (alive <= dead) return dead;
			}

			assert(entities_[alive] != internal::null_version);
			assert(entities_[dead] == internal::null_version);

			std::swap(entities_[alive], entities_[dead]);

			++dead; --alive;
		}
		return dead;
	}
	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class ManagerBasics;
	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class EntityBasics;

	friend struct Entity;

	internal::Id size_{ 0 };
	internal::Id next_size_{ 0 };
	std::vector<internal::Version> entities_{ internal::null_version };
	std::vector<std::unique_ptr<internal::BasePool>> component_pools_;
	std::vector<std::unique_ptr<internal::BaseSystem>> systems_;
	static internal::Id& ComponentCount() { static internal::Id id{ 0 }; return id; }
	static internal::Id& SystemCount() { static internal::Id id{ 0 }; return id; }
};

struct Entity {
	Entity() = default;
	~Entity() = default;
	Entity(Entity&&) = default;
	Entity& operator=(Entity&&) = default;
	Entity(const Entity&) = default;
	Entity& operator=(const Entity&) = default;
	bool operator==(const Entity& other) const {
		return id_ == other.id_ && version_ == other.version_ && manager_ == other.manager_;
	}
	bool operator!=(const Entity& other) const {
		return !operator==(other);
	}
	void Destroy() {
		if (manager_ != nullptr) {
			manager_->Destroy(id_);
			version_ = internal::null_version;
		}
	}
private:

	// TODO: TEMPORARY: ONLY FOR TESTS
	friend class EntityBasics;

	friend class Manager;
	friend struct NullEntity;
	Entity(Manager* manager, internal::Id id, internal::Version version) : manager_{ manager }, id_{ id }, version_{ version } {}
	Manager* const manager_{ nullptr };
	const internal::Id id_{ 0 };
	internal::Version version_{ internal::null_version };
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
		return entity.version_ == internal::null_version;
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
	GrowEntitiesIfNeeded();
	internal::Id id{ next_size_++ };

	assert(id < entities_.size());
	auto& entity_version = entities_[id];
	assert(entity_version == internal::null_version);

	return Entity{ this, id, ++entity_version };
}

} // namespace ecs