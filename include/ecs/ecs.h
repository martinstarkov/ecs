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

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <limits>
#include <type_traits>
#include <functional>
#include <deque>
#include <memory>
#include <cassert>

namespace ecs {

class Entity;
class Manager;

namespace impl {

class NullEntity;
class NullManager;
class PoolManager;

using Index = std::size_t;

using Version = std::uint32_t;

inline constexpr Version null_version{ 0 };

class PoolInterface {
public:
    virtual ~PoolInterface() = default;
    virtual std::shared_ptr<impl::PoolInterface> Clone() const = 0;
    virtual void Copy(Index from_entity, Index to_entity) = 0;
    virtual void Clear() = 0;
    virtual void Reset() = 0;
    virtual bool Remove(Index entity) = 0;
    virtual bool Has(Index entity) const = 0;
    virtual Index GetId() const = 0;
};

template <typename T>
class Pool : public PoolInterface {
public:
    Pool() = default;
    Pool(const std::vector<T>& components,
         const std::vector<Index>& dense,
         const std::vector<Index>& sparse);
    ~Pool() = default;
    Pool(Pool&&) = default;
    Pool& operator=(Pool&&) = default;
    Pool(const Pool&) = default;
    Pool& operator=(const Pool&) = default;
    virtual std::shared_ptr<impl::PoolInterface> Clone() const final;
    virtual void Copy(Index from_entity, Index to_entity) final;
    virtual void Clear() final;
    virtual void Reset() final;
    virtual bool Remove(Index entity) final;
    virtual bool Has(Index entity) const final;
    virtual Index GetId() const final;
    const T& Get(Index entity) const;
    T& Get(Index entity);
    template <typename ...Ts>
    T& Add(Index entity, Ts&&... constructor_args);
private:
    std::vector<T> components;
    std::vector<Index> dense;
    std::vector<Index> sparse;
};

// Modified version of: https://github.com/syoyo/dynamic_bitset/blob/master/dynamic_bitset.hh

class DynamicBitset {
public:
    DynamicBitset() = default;
    ~DynamicBitset() = default;
    DynamicBitset(DynamicBitset&&) = default;
    DynamicBitset& operator=(DynamicBitset&&) = default;
    DynamicBitset(const DynamicBitset&) = default;
    DynamicBitset& operator=(const DynamicBitset&) = default;

    void Set(std::size_t index, bool value = true) {
        std::size_t byte_index{ index / 8 };
        std::uint8_t offset{ static_cast<std::uint8_t>(index % 8) };
        std::uint8_t bitfield = static_cast<std::uint8_t>(1 << offset);

        assert(byte_index < data_.size());

        if (value)
            data_[byte_index] |= bitfield;
        else
            data_[byte_index] &= (~bitfield);
    }

    bool operator[](std::size_t index) const {
        std::size_t byte_index{ index / 8 };
        std::size_t offset{ index % 8 };

        assert(byte_index < data_.size());

        return (data_[byte_index] >> offset) & 0x1;
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

class PoolManager {
public:
    PoolManager();
    ~PoolManager() = default;
    PoolManager(PoolManager&&) = default;
    PoolManager& operator=(PoolManager&&) = default;
    PoolManager(const PoolManager&) = default;
    PoolManager& operator=(const PoolManager&) = default;
    bool operator==(const PoolManager& other) const;
    bool operator!=(const PoolManager& other) const;
    std::shared_ptr<impl::PoolManager> Clone() const;
    void Clear();
    void Refresh();
    void Reserve(std::size_t capacity);
    void Reset();
    Entity CreateEntity(Manager* manager);
    template <typename ...Ts>
    Entity CopyEntity(const Entity& e, Manager* manager);
    void ForEachEntity(std::function<void(Entity)> function, Manager* manager);

    // Contemplate: Is there a way to do the following functions using std::function?
    // According to https://stackoverflow.com/a/5153276, there is not.

    template <typename ...Ts, typename T>
    void ForEachEntityWith(T function, Manager* manager);
    template <typename ...Ts, typename T>
    void ForEachEntityWithout(T function, Manager* manager);
    std::size_t Size() const;
    std::size_t Capacity() const;
private:
    template <typename T>
    friend class impl::Pool;
    friend class Manager;
    friend class Entity;
    void ClearEntity(impl::Index entity);
    bool IsAlive(impl::Index entity, impl::Version version) const;
    bool Match(impl::Index entity1, impl::Index entity2);
    void DestroyEntity(impl::Index entity, impl::Version version);
    void Resize(std::size_t size);
    template <typename T>
    const impl::Pool<T>* GetPool(impl::Index component) const;
    template <typename T>
    impl::Pool<T>* GetPool(impl::Index component);
    template <typename ...Ts>
    decltype(auto) Get(impl::Index entity) const;
    template <typename ...Ts>
    decltype(auto) Get(impl::Index entity);
    template <typename T>
    bool Has(impl::Index entity, impl::Index component) const;
    template <typename T, typename ...Ts>
    T& Add(impl::Index entity, impl::Index component, Ts&&... constructor_args);
    template <typename T>
    void Remove(impl::Index entity, impl::Index component);
    template <typename T>
    static impl::Index GetId();
    static impl::Index& ComponentCount();
    impl::Index next_entity_{ 0 };
    impl::Index count_{ 0 };
    bool refresh_required_{ false };
    impl::DynamicBitset entities_;
    impl::DynamicBitset refresh_;
    std::vector<impl::Version> versions_;
    // TODO: Move to using smart pointers.
    std::vector<std::shared_ptr<impl::PoolInterface>> pools_;
    std::deque<impl::Index> free_entities_;
};

} // namespace impl

class Manager {
public:
    Manager() : pool_manager_{ std::make_shared<impl::PoolManager>() } {}
    Manager(const impl::NullManager& null_manager);

    ~Manager() = default;

    Manager(Manager&&) = default;
    Manager& operator=(Manager&&) = default;
    Manager(const Manager&) = default;
    Manager& operator=(const Manager&) = default;

    bool operator==(const Manager& o) const {
        return *pool_manager_ == *o.pool_manager_;
    }

    bool operator!=(const Manager& o) const {
        return !(*this == o);
    }

    Manager Clone() const {
        assert(pool_manager_ != nullptr && "Cannot clone manager with null pool manager");
        return Manager{ pool_manager_->Clone() };
    }

    void Clear() {
        assert(pool_manager_ != nullptr && "Cannot clear manager with null pool manager");
        pool_manager_->Clear();
    }

    void Refresh() {
        assert(pool_manager_ != nullptr && "Cannot refresh manager with null pool manager");
        pool_manager_->Refresh();
    }

    void Reserve(std::size_t capacity) {
        assert(pool_manager_ != nullptr && "Cannot reserve manager with null pool manager");
        pool_manager_->Reserve(capacity);
    }

    void Reset() {
        assert(pool_manager_ != nullptr && "Cannot reset manager with null pool manager");
        pool_manager_->Reset();
    }

    Entity CreateEntity();

    template <typename ...Ts>
    Entity CopyEntity(const Entity& e);

    void ForEachEntity(std::function<void(Entity)> function) {
        assert(pool_manager_ != nullptr && "Cannot cycle through entities for a manager with null pool manager");
        pool_manager_->ForEachEntity(function, this);
    }

    template <typename ...Ts, typename T>
    void ForEachEntityWith(T function) {
        assert(pool_manager_ != nullptr && "Cannot cycle through entities with a component for a manager with null pool manager");
        pool_manager_->ForEachEntityWith<Ts...>(function, this);
    }

    template <typename ...Ts, typename T>
    void ForEachEntityWithout(T function) {
        assert(pool_manager_ != nullptr && "Cannot cycle through entities without a component for a manager with null pool manager");
        pool_manager_->ForEachEntityWithout<Ts...>(function, this);
    }

    std::size_t Size() const {
        assert(pool_manager_ != nullptr && "Cannot get size of manager with null pool manager");
        return pool_manager_->Size();
    }

    std::size_t Capacity() const {
        assert(pool_manager_ != nullptr && "Cannot get capacity of manager with null pool manager");
        return pool_manager_->Capacity();
    }
private:

    friend struct std::hash<ecs::Entity>;
    friend class impl::NullManager;
    friend class Entity;
    Manager(std::shared_ptr<impl::PoolManager> pool_manager) : pool_manager_{ pool_manager } {}
    std::shared_ptr<impl::PoolManager> pool_manager_{ nullptr };
};

namespace impl {

class NullManager {
public:
    operator Manager() const;
    constexpr bool operator==(const NullManager&) const;
    constexpr bool operator!=(const NullManager&) const;
    bool operator==(const Manager& e) const;
    bool operator!=(const Manager& e) const;
};

} // namespace impl

inline constexpr impl::NullManager null_manager{};

class Entity {
public:
    Entity() = default;
    ~Entity() = default;

    Entity& operator=(const Entity&) = default;
    Entity(const Entity&) = default;
    Entity& operator=(Entity&&) = default;
    Entity(Entity&&) = default;

    bool operator==(const Entity& e) const;
    bool operator!=(const Entity& e) const;

    bool IsIdenticalTo(const Entity& e) const;

    Manager GetManager();

    // @return Tuple of references to requested components, or a single reference if only one component is requested.
    template <typename ...Ts>
    decltype(auto) Get() const;
    // @return Tuple of references to requested components, or a single reference if only one component is requested.
    template <typename ...Ts>
    decltype(auto) Get();

    template <typename T, typename ...Ts>
    T& Add(Ts&&... constructor_args);

    template <typename ...Ts>
    bool Has() const;

    template <typename ...Ts>
    bool HasAny() const;

    template <typename ...Ts>
    void Remove();

    void Clear();
    void Destroy();
    bool IsAlive() const;
private:
    friend class impl::PoolManager;
    friend class impl::NullEntity;
    friend struct std::hash<Entity>;
    Entity(impl::Index entity, impl::Version version, Manager* manager);
    impl::Index entity_{ 0 };
    impl::Version version_{ impl::null_version };
    // TODO: Move to using smart pointers.
    Manager* manager_{ nullptr };
};

namespace impl {

class NullEntity {
public:
    operator Entity() const;
    constexpr bool operator==(const NullEntity&) const;
    constexpr bool operator!=(const NullEntity&) const;
    bool operator==(const Entity& e) const;
    bool operator!=(const Entity& e) const;
};

} // namespace impl

inline constexpr impl::NullEntity null{};

template <typename T>
inline impl::Pool<T>::Pool(const std::vector<T>& components,
                           const std::vector<Index>& dense,
                           const std::vector<Index>& sparse) :
    components{ components }, dense{ dense }, sparse{ sparse } {}

template <typename T>
inline std::shared_ptr<impl::PoolInterface> impl::Pool<T>::Clone() const {
    static_assert(std::is_copy_constructible_v<T>,
                  "Cannot clone component pool with a non copy-constructible component");
    return std::make_shared<Pool<T>>(components, dense, sparse);
}

template <typename T>
inline void impl::Pool<T>::Copy(Index from_entity, Index to_entity) {
    static_assert(std::is_copy_constructible_v<T>,
                  "Cannot copy component in a pool of non copy constructible components");
    assert(Has(from_entity));
    if (Has(to_entity))
        components[sparse[to_entity]] = components[sparse[from_entity]];
    else
        Add(to_entity, components[sparse[from_entity]]);
}

template <typename T>
inline void impl::Pool<T>::Clear() {
    components.clear();
    dense.clear();
    sparse.clear();
}

template <typename T>
inline void impl::Pool<T>::Reset() {
    Clear();

    components.shrink_to_fit();
    dense.shrink_to_fit();
    sparse.shrink_to_fit();
}

template <typename T>
inline bool impl::Pool<T>::Remove(Index entity) {
    if (Has(entity)) {
        // See https://skypjack.github.io/2020-08-02-ecs-baf-part-9/ for in-depth explanation.
        // In short, swap with back and pop back, relinking sparse ids after.
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

template <typename T>
inline bool impl::Pool<T>::Has(Index entity) const {
    if (entity >= sparse.size()) return false;
    auto s = sparse[entity];
    if (s >= dense.size()) return false;
    return entity == dense[s];
}

template <typename T>
inline impl::Index impl::Pool<T>::GetId() const {
    return ecs::impl::PoolManager::GetId<T>();
}

template <typename T>
inline const T& impl::Pool<T>::Get(Index entity) const {
    /*
    * Debug tip:
    * If you ended up here and want to find out which entity triggered
    * this assertion, set breakpoints or follow the call stack.
    */
    assert(Has(entity) && "Cannot get a component which an entity does not have");
    return components[sparse[entity]];
}

template <typename T>
inline T& impl::Pool<T>::Get(Index entity) {
    return const_cast<T&>(static_cast<const Pool<T>&>(*this).Get(entity));
}

template <typename T>
template <typename ...Ts>
inline T& impl::Pool<T>::Add(Index entity, Ts&&... constructor_args) {
    static_assert(std::is_constructible_v<T, Ts...>,
                  "Cannot add component which is not constructible from given arguments");
    static_assert(std::is_move_constructible_v<T>,
                  "Cannot add component which is not move constructible");
    static_assert(std::is_destructible_v<T>,
                  "Cannot add component which is not destructible");
    if (entity < sparse.size()) { // Entity has had the component before.
        if (sparse[entity] < dense.size() &&
            dense[sparse[entity]] == entity) { // Entity currently has the component.
            // Replace the current component with a new component.
            T& component{ components[sparse[entity]] };
            // This approach prevents the creation of a temporary component object.
            component.~T();
            new(&component) T(std::forward<Ts>(constructor_args)...);
            return component;
        }
        // Entity currently does not have the component.
        sparse[entity] = dense.size();
    } else
        // Entity has never had the component.
        sparse.resize(entity + 1, dense.size());
    // Add new component to the entity.
    dense.push_back(entity);
    return components.emplace_back(std::forward<Ts>(constructor_args)...);
}

inline impl::PoolManager::PoolManager() {
    // Reserve capacity for 1 entity so that manager size will double in powers of 2.
    Reserve(1);
}

inline bool impl::PoolManager::operator==(const impl::PoolManager& other) const {
    // TODO: Check this to be accurate comparison.
    return next_entity_ == other.next_entity_ &&
           count_ == other.count_ &&
           refresh_required_ == other.refresh_required_ &&
           entities_ == other.entities_ &&
           refresh_ == other.refresh_ &&
           versions_ == other.versions_ &&
           pools_ == other.pools_ &&
           free_entities_ == other.free_entities_;
}

inline bool impl::PoolManager::operator!=(const impl::PoolManager& other) const {
    return !operator==(other);
}

inline std::shared_ptr<impl::PoolManager> impl::PoolManager::Clone() const {
    std::shared_ptr<impl::PoolManager> clone = std::make_shared<impl::PoolManager>();
    clone->count_ = count_;
    clone->next_entity_ = next_entity_;
    clone->entities_ = entities_;
    clone->refresh_ = refresh_;
    clone->refresh_required_ = refresh_required_;
    clone->versions_ = versions_;
    clone->free_entities_ = free_entities_;
    clone->pools_.resize(pools_.size(), nullptr);
    for (std::size_t i{ 0 }; i < pools_.size(); ++i) {
        auto& pool{ pools_[i] };
        if (pool != nullptr)
            clone->pools_[i] = pool->Clone();
    }
    assert(*clone == *this && "Cloning manager failed");
    return clone;
}

template <typename T>
inline const impl::Pool<T>* impl::PoolManager::GetPool(impl::Index component) const {
    assert(component == GetId<T>() && "GetPool mismatch with component id");
    if (component < pools_.size())
        // This is nullptr if the pool does not exist in the manager.
        return static_cast<const impl::Pool<T>*>(pools_[component].get());
    return nullptr;
}

template <typename T>
inline impl::Pool<T>* impl::PoolManager::GetPool(impl::Index component) {
    return const_cast<impl::Pool<T>*>(static_cast<const impl::PoolManager&>(*this).GetPool<T>(component));
}

template <typename T, typename ...Ts>
inline T& impl::PoolManager::Add(impl::Index entity, impl::Index component, Ts&&... constructor_args) {
    if (component >= pools_.size())
        pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
    auto pool{ GetPool<T>(component) };
    if (pool == nullptr) {
        pools_[component] = std::make_shared<impl::Pool<T>>();
        pool = static_cast<impl::Pool<T>*>(pools_[component].get());
    }
    assert(pool != nullptr && "Could not create new component pool correctly");
    return pool->Add(entity, std::forward<Ts>(constructor_args)...);
}

template <typename T>
inline impl::Index impl::PoolManager::GetId() {
    // Get the next available id save that id as static variable for the component type.
    static impl::Index id{ ComponentCount()++ };
    return id;
}

inline impl::Index& impl::PoolManager::ComponentCount() {
    static impl::Index id{ 0 };
    return id;
}

inline bool impl::PoolManager::IsAlive(impl::Index entity, impl::Version version) const {
    return version != impl::null_version && entity < versions_.size() &&
           versions_[entity] == version  && entity < entities_.Size() &&
           // Entity considered currently alive or entity marked
           // for creation/deletion but not yet created/deleted.
           (entities_[entity] || refresh_[entity]);
}

inline void impl::PoolManager::ClearEntity(impl::Index entity) {
    for (auto& pool : pools_)
        if (pool != nullptr)
            pool->Remove(entity);
}

template <typename T>
inline void impl::PoolManager::Remove(impl::Index entity, impl::Index component) {
    auto pool{ GetPool<T>(component) };
    if (pool != nullptr)
        pool->impl::template Pool<T>::Remove(entity);
}

template <typename T>
inline bool impl::PoolManager::Has(impl::Index entity, impl::Index component) const {
    const auto pool{ GetPool<T>(component) };
    return pool != nullptr && pool->Has(entity);
}

template <typename ...Ts>
inline decltype(auto) impl::PoolManager::Get(impl::Index entity) const {
    if constexpr (sizeof...(Ts) == 1) {
        const auto pool{ GetPool<Ts...>(GetId<Ts...>()) };
        assert(pool != nullptr && "Manager does not have the requested component");
        return pool->Get(entity);
    } else {
        const auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
        assert(((std::get<impl::Pool<Ts>*>(pools) != nullptr) && ...) &&
               "Manager does not have at least one of the requested components");
        return std::forward_as_tuple<const Ts&...>((std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Get(entity))...);
    }
}

template <typename ...Ts>
inline decltype(auto) impl::PoolManager::Get(impl::Index entity) {
    if constexpr (sizeof...(Ts) == 1) {
        auto pool{ GetPool<Ts...>(GetId<Ts...>()) };
        assert(pool != nullptr && "Manager does not have the requested component");
        return pool->Get(entity);
    } else {
        auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
        assert(((std::get<impl::Pool<Ts>*>(pools) != nullptr) && ...) &&
               "Manager does not have at least one of the requested components");
        return std::forward_as_tuple<Ts&...>((std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Get(entity))...);
    }
}

inline Entity impl::PoolManager::CreateEntity(Manager* manager) {
    impl::Index entity{ 0 };
    // Pick entity from free list before trying to increment entity counter.
    if (free_entities_.size() > 0) {
        entity = free_entities_.front();
        free_entities_.pop_front();
    } else
        entity = next_entity_++;
    // Double the size of the manager if capacity is reached.
    if (entity >= entities_.Size())
        Resize(versions_.capacity() * 2);
    assert(entity < entities_.Size() &&
           "Created entity is outside of manager entity vector range");
    assert(!entities_[entity] &&
           "Cannot create new entity from live entity");
    assert(!refresh_[entity] &&
           "Cannot create new entity from refresh marked entity");
    // Mark entity for refresh.
    refresh_.Set(entity, true);
    refresh_required_ = true;
    // Entity version incremented here.
    return { entity, ++versions_[entity], manager };
}

template <typename ...Ts>
inline Entity impl::PoolManager::CopyEntity(const Entity& e, Manager* manager) {
    // Create new entity in the manager to copy to.
    // TODO: Consider making this creation optional as sometimes it
    // is more intuitive to create the entity outside this function.
    Entity copy_entity{ CreateEntity(manager) };
    impl::Index from{ e.entity_ };
    impl::Index to{ copy_entity.entity_ };
    if constexpr (sizeof...(Ts) > 0) { // Copy only specific components.
        static_assert(std::conjunction_v<std::is_copy_constructible<Ts>...>,
                      "Cannot copy entity with a component that is not copy constructible");
        auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
        bool manager_has{ ((std::get<impl::Pool<Ts>*>(pools) != nullptr)  && ...) };
        assert(manager_has &&
               "Cannot copy entity with a component that is not even in the manager");
        bool entity_has{ (std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Has(from) && ...) };
        assert(entity_has &&
               "Cannot copy entity with a component that it does not have");
        (std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Copy(from, to), ...);
    } else // Copy all components.
        for (auto& pool : pools_)
            if (pool != nullptr && pool->Has(from))
                pool->Copy(from, to);
    return copy_entity;
}

inline void impl::PoolManager::Refresh() {
    if (refresh_required_) {
        // This must be set before refresh starts in case
        // events are called (for instance during entity deletion).
        refresh_required_ = false;
        assert(entities_.Size() == versions_.size() &&
               "Refresh failed due to varying entity vector and version vector size");
        assert(entities_.Size() == refresh_.Size() &&
               "Refresh failed due to varying entity vector and refresh vector size");
        assert(next_entity_ <= entities_.Size() &&
               "Next available entity must not be out of bounds of entity vector");
        impl::Index alive{ 0 };
        impl::Index dead{ 0 };
        for (impl::Index entity{ 0 }; entity < next_entity_; ++entity) {
            // Entity was marked for refresh.
            if (refresh_[entity]) {
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
        }
        assert(alive >= 0 && dead >= 0);
        // Update entity count with net change.
        count_ += alive - dead;
        assert(count_ >= 0);
    }
}

inline void impl::PoolManager::Reserve(std::size_t capacity) {
    entities_.Reserve(capacity);
    refresh_.Reserve(capacity);
    versions_.reserve(capacity);
    assert(entities_.Capacity() == refresh_.Capacity() &&
           "Entity and refresh vectors must have the same capacity");
}

inline void impl::PoolManager::Resize(std::size_t size) {
    if (size > entities_.Size()) {
        entities_.Resize(size, false);
        refresh_.Resize(size, false);
        versions_.resize(size, impl::null_version);
    }
    assert(entities_.Size() == versions_.size() &&
           "Resize failed due to varying entity vector and version vector size");
    assert(entities_.Size() == refresh_.Size() &&
           "Resize failed due to varying entity vector and refresh vector size");
}

inline void impl::PoolManager::Clear() {
    count_ = 0;
    next_entity_ = 0;
    refresh_required_ = false;

    entities_.Clear();
    refresh_.Clear();
    versions_.clear();
    free_entities_.clear();

    for (auto& pool : pools_) {
        if (pool != nullptr) {
            pool->Clear();
        }
    }
}

inline void impl::PoolManager::Reset() {
    Clear();

    entities_.ShrinkToFit();
    refresh_.ShrinkToFit();
    versions_.shrink_to_fit();
    free_entities_.shrink_to_fit();

    pools_.clear();
    pools_.shrink_to_fit();

    Reserve(1);
}

inline void impl::PoolManager::DestroyEntity(impl::Index entity, impl::Version version) {
    assert(entity < versions_.size());
    assert(entity < refresh_.Size());
    if (versions_[entity] == version) {
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
}

inline bool impl::PoolManager::Match(impl::Index entity1, impl::Index entity2) {
    for (auto& pool : pools_)
        if (pool != nullptr) {
            bool has1{ pool->Has(entity1) };
            bool has2{ pool->Has(entity2) };
            // Check that one entity has a component while the other doesn't.
            if ((has1 || has2) && (!has1 || !has2))
                // Exit early if one non-matching component is found.
                return false;
        }
    return true;
}

inline std::size_t impl::PoolManager::Size() const {
    return count_;
}

inline std::size_t impl::PoolManager::Capacity() const {
    return versions_.capacity();
}

inline void impl::PoolManager::ForEachEntity(std::function<void(Entity)> function, Manager* manager) {
    assert(entities_.Size() == versions_.size() &&
           "Cannot loop through manager entities if and version and entity vectors differ in size");
    assert(next_entity_ <= entities_.Size() &&
           "Last entity must be within entity vector range");
    for (impl::Index entity{ 0 }; entity < next_entity_; ++entity)
        if (entities_[entity])
            function(Entity{ entity, versions_[entity], manager });
}

template <typename ...Ts, typename T>
inline void impl::PoolManager::ForEachEntityWith(T function, Manager* manager) {
    static_assert(sizeof ...(Ts) > 0,
                  "Cannot loop through each entity without providing at least one component type");
    assert(entities_.Size() == versions_.size() &&
           "Cannot loop through manager entities if and version and entity vectors differ in size");
    assert(next_entity_ <= entities_.Size() &&
           "Last entity must be within entity vector range");
    auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
    // Check that none of the requested component pools are nullptrs.
    if (((std::get<impl::Pool<Ts>*>(pools) != nullptr) && ...))
        for (impl::Index entity{ 0 }; entity < next_entity_; ++entity)
            // If entity is alive and has the components, call lambda on it.
            if (entities_[entity] && (std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Has(entity) && ...))
                    function(Entity{ entity, versions_[entity], manager },
                             (std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Get(entity))...);
}

template <typename ...Ts, typename T>
inline void impl::PoolManager::ForEachEntityWithout(T function, Manager* manager) {
    assert(entities_.Size() == versions_.size() &&
           "Cannot loop through manager entities if and version and entity vectors differ in size");
    assert(next_entity_ <= entities_.Size() &&
           "Last entity must be within entity vector range");
    auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
    // Check that none of the requested component pools are nullptrs.
    if (((std::get<impl::Pool<Ts>*>(pools) != nullptr) && ...))
        for (impl::Index entity{ 0 }; entity < next_entity_; ++entity)
            // If entity is alive and does not have one of the components, call lambda on it.
            if (entities_[entity] && (!std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Has(entity) || ...))
                function(Entity{ entity, versions_[entity], manager });
}

inline Manager::Manager(const impl::NullManager& null_manager) : pool_manager_{ nullptr } {}

inline Entity Manager::CreateEntity() {
    assert(pool_manager_ != nullptr && "Cannot create an entity for a manager with null pool manager");
    return pool_manager_->CreateEntity(this);
}

template <typename ...Ts>
inline Entity Manager::CopyEntity(const Entity& e) {
    assert(pool_manager_ != nullptr && "Cannot copy entity for a manager with null pool manager");
    return pool_manager_->CopyEntity<Ts...>(e, this);
}

template <typename T, typename ...Ts>
inline T& Entity::Add(Ts&&... constructor_args) {
    assert(IsAlive() && "Cannot add component to dead or null entity");
    return manager_->pool_manager_->Add<T>(entity_, manager_->pool_manager_->GetId<T>(), std::forward<Ts>(constructor_args)...);
}

template <typename ...Ts>
inline void Entity::Remove() {
    assert(IsAlive() && "Cannot remove component(s) from dead or null entity");
    (manager_->pool_manager_->Remove<Ts>(entity_, manager_->pool_manager_->GetId<Ts>()), ...);
}

template <typename ...Ts>
inline bool Entity::Has() const {
    assert(IsAlive() && "Cannot check if dead or null entity has component(s)");
    return IsAlive() && (manager_->pool_manager_->Has<Ts>(entity_, manager_->pool_manager_->GetId<Ts>()) && ...);
}

template <typename ...Ts>
inline bool Entity::HasAny() const {
    assert(IsAlive() && "Cannot check if dead or null entity has any component(s)");
    return IsAlive() && (manager_->pool_manager_->Has<Ts>(entity_, manager_->pool_manager_->GetId<Ts>()) || ...);
}

template <typename ...Ts>
inline decltype(auto) Entity::Get() const {
    assert(IsAlive() && "Cannot get component(s) from dead or null entity");
    return manager_->pool_manager_->Get<Ts...>(entity_);
}

template <typename ...Ts>
inline decltype(auto) Entity::Get() {
    assert(IsAlive() && "Cannot get component(s) from dead or null entity");
    return manager_->pool_manager_->Get<Ts...>(entity_);
}

inline void Entity::Clear() {
    assert(IsAlive() && "Cannot clear components of dead or null entity");
    manager_->pool_manager_->ClearEntity(entity_);
}

inline bool Entity::IsAlive() const {
    return manager_ != nullptr && manager_->pool_manager_ != nullptr && manager_->pool_manager_->IsAlive(entity_, version_);
}

inline void Entity::Destroy() {
    if (IsAlive())
        manager_->pool_manager_->DestroyEntity(entity_, version_);
}

inline Manager Entity::GetManager() {
    assert(manager_ != nullptr && "Cannot return parent manager of a null entity");
    return *manager_;
}

inline bool Entity::operator==(const Entity& e) const {
    return
        entity_ == e.entity_ &&
        version_ == e.version_ &&
        manager_ == e.manager_;
}

inline bool Entity::operator!=(const Entity& e) const {
    return !(*this == e);
}

inline Entity::Entity(impl::Index entity, impl::Version version, Manager* manager) :
    entity_{ entity },
    version_{ version },
    manager_{ manager } {}

inline bool Entity::IsIdenticalTo(const Entity& e) const {
    return
        *this == e ||
        (*this == ecs::null && e == ecs::null) ||
        (*this != ecs::null && e != ecs::null && manager_ == e.manager_ && manager_ != nullptr && entity_ != e.entity_ &&
        manager_->pool_manager_->Match(entity_, e.entity_));
}

inline impl::NullEntity::operator Entity() const {
    return Entity{};
}

inline constexpr bool impl::NullEntity::operator==(const impl::NullEntity&) const {
    return true;
}

inline constexpr bool impl::NullEntity::operator!=(const impl::NullEntity&) const {
    return false;
}

inline bool impl::NullEntity::operator==(const Entity& e) const {
    return e.version_ == impl::null_version;
}

inline bool impl::NullEntity::operator!=(const Entity& e) const {
    return !(*this == e);
}

inline bool operator==(const Entity& e, const impl::NullEntity& null_e) {
    return null_e == e;
}

inline bool operator!=(const Entity& e, const impl::NullEntity& null_e) {
    return !(null_e == e);
}

inline impl::NullManager::operator Manager() const {
    return Manager{};
}

inline constexpr bool impl::NullManager::operator==(const impl::NullManager&) const {
    return true;
}

inline constexpr bool impl::NullManager::operator!=(const impl::NullManager&) const {
    return false;
}

inline bool impl::NullManager::operator==(const Manager& e) const {
    return e.pool_manager_ == nullptr;
}

inline bool impl::NullManager::operator!=(const Manager& e) const {
    return !(*this == e);
}

inline bool operator==(const Manager& e, const impl::NullManager& null_e) {
    return null_e == e;
}

inline bool operator!=(const Manager& e, const impl::NullManager& null_e) {
    return !(null_e == e);
}

} // namespace ecs

namespace std {

template <>
struct hash<ecs::Entity> {
    std::size_t operator()(const ecs::Entity& e) const {
        // Source: https://stackoverflow.com/a/17017281
        std::size_t hash{ 17 };
        hash = hash * 31 + std::hash<ecs::impl::PoolManager*>()(e.manager_->pool_manager_.get());
        hash = hash * 31 + std::hash<ecs::impl::Index>()(e.entity_);
        hash = hash * 31 + std::hash<ecs::impl::Version>()(e.version_);
        return hash;
    }
};

} // namespace std
