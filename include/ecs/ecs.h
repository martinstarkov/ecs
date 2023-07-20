/*

MIT License

Copyright (c) 2023 | Martin Starkov | https://github.com/martinstarkov

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

#include <cstdlib>     // std::size_t
#include <cstdint>     // std::uint32_t
#include <vector>      // std::vector
#include <limits>      // std::numeric_limits
#include <type_traits> // std::enable_if_t
#include <functional>  // std::hash
#include <deque>       // std::deque
#include <cassert>     // assert

namespace ecs {

class Entity;
class Manager;

namespace impl {

class NullEntity;

using Index = std::size_t;

using Version = std::uint32_t;

inline constexpr Version null_version{ 0 };

class PoolInterface {
public:
    virtual ~PoolInterface() = default;
    virtual PoolInterface* Clone() const = 0;
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
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    virtual PoolInterface* Clone() const final;
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

} // namespace impl

class Manager {
public:
    Manager();
    ~Manager();
    Manager(Manager&&) = default;
    Manager& operator=(Manager&&) = default;
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    bool operator==(const Manager& other) const;
    bool operator!=(const Manager& other) const;
    Manager Clone() const;
    void Clear();
    void Refresh();
    void Reserve(std::size_t capacity);
    void Reset();
    Entity CreateEntity();
    template <typename ...Ts>
    Entity CopyEntity(const Entity& e);
    template <typename T>
    void ForEachEntity(T function);
    template <typename ...Ts, typename T>
    void ForEachEntityWith(T function);
    template <typename ...Ts, typename T>
    void ForEachEntityWithout(T function);
    std::size_t Size() const;
    std::size_t Capacity() const;
private:
    void ClearEntity(impl::Index entity);
    void DestroyPools();
    void Resize(std::size_t size);
    void DestroyEntity(impl::Index entity, impl::Version version);
    bool IsAlive(impl::Index entity, impl::Version version) const;
    bool Match(impl::Index entity1, impl::Index entity2);
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
    template <typename T>
    friend class impl::Pool;
    friend class Entity;
    impl::Index next_entity_{ 0 };
    impl::Index count_{ 0 };
    bool refresh_required_{ false };
    std::vector<bool> entities_;
    std::vector<bool> refresh_;
    std::vector<impl::Version> versions_;
    // TODO: Move to using smart pointers.
    std::vector<impl::PoolInterface*> pools_;
    std::deque<impl::Index> free_entities_;
};

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
    const Manager& GetManager() const;
    Manager& GetManager();
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
    friend class Manager;
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
inline impl::PoolInterface* impl::Pool<T>::Clone() const {
    static_assert(std::is_copy_constructible_v<T>,
                  "Cannot clone component pool with a non copy-constructible component");
    return new Pool<T>(components, dense, sparse);
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
    return entity < sparse.size() &&
           sparse[entity] < dense.size() &&
           dense[sparse[entity]] == entity;
}

template <typename T>
inline impl::Index impl::Pool<T>::GetId() const {
    return ecs::Manager::GetId<T>();
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

inline Manager::Manager() {
    // Reserve capacity for 1 entity so that manager size will double in powers of 2.
    Reserve(1);
}

inline Manager::~Manager() {
    DestroyPools();
}

inline bool Manager::operator==(const Manager& other) const {
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

inline bool Manager::operator!=(const Manager& other) const {
    return !operator==(other);
}

inline Manager Manager::Clone() const {
    Manager clone;
    clone.count_ = count_;
    clone.next_entity_ = next_entity_;
    clone.entities_ = entities_;
    clone.refresh_ = refresh_;
    clone.refresh_required_ = refresh_required_;
    clone.versions_ = versions_;
    clone.free_entities_ = free_entities_;
    clone.pools_.resize(pools_.size(), nullptr);
    for (std::size_t i{ 0 }; i < pools_.size(); ++i) {
        auto pool{ pools_[i] };
        if (pool != nullptr)
            clone.pools_[i] = pool->Clone();
    }
    assert(clone == *this && "Cloning manager failed");
    return clone;
}

template <typename T>
inline const impl::Pool<T>* Manager::GetPool(impl::Index component) const {
    assert(component == GetId<T>() && "GetPool mismatch with component id");
    if (component < pools_.size())
        // This is nullptr if the pool does not exist in the manager.
        return static_cast<const impl::Pool<T>*>(pools_[component]);
    return nullptr;
}

template <typename T>
inline impl::Pool<T>* Manager::GetPool(impl::Index component) {
    return const_cast<impl::Pool<T>*>(static_cast<const Manager&>(*this).GetPool<T>(component));
}

template <typename T, typename ...Ts>
inline T& Manager::Add(impl::Index entity, impl::Index component, Ts&&... constructor_args) {
    if (component >= pools_.size())
        pools_.resize(static_cast<std::size_t>(component) + 1, nullptr);
    auto pool{ GetPool<T>(component) };
    if (pool == nullptr) {
        pool = new impl::Pool<T>();
        pools_[component] = pool;
    }
    assert(pool != nullptr && "Could not create new component pool correctly");
    return pool->Add(entity, std::forward<Ts>(constructor_args)...);
}

template <typename T>
inline impl::Index Manager::GetId() {
    // Get the next available id save that id as static variable for the component type.
    static impl::Index id{ ComponentCount()++ };
    return id;
}

inline impl::Index& Manager::ComponentCount() {
    static impl::Index id{ 0 };
    return id;
}

inline bool Manager::IsAlive(impl::Index entity, impl::Version version) const {
    return version != impl::null_version && entity < versions_.size() &&
           versions_[entity] == version  && entity < entities_.size() &&
           // Entity considered currently alive or entity marked
           // for creation/deletion but not yet created/deleted.
           (entities_[entity] || refresh_[entity]);
}

inline void Manager::ClearEntity(impl::Index entity) {
    for (auto pool : pools_)
        if (pool != nullptr)
            pool->Remove(entity);
}

template <typename T>
inline void Manager::Remove(impl::Index entity, impl::Index component) {
    auto pool{ GetPool<T>(component) };
    if (pool != nullptr)
        pool->impl::template Pool<T>::Remove(entity);
}

template <typename T>
inline bool Manager::Has(impl::Index entity, impl::Index component) const {
    const auto pool{ GetPool<T>(component) };
    return pool != nullptr && pool->Has(entity);
}

template <typename ...Ts>
inline decltype(auto) Manager::Get(impl::Index entity) const {
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
inline decltype(auto) Manager::Get(impl::Index entity) {
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

inline Entity Manager::CreateEntity() {
    impl::Index entity{ 0 };
    // Pick entity from free list before trying to increment entity counter.
    if (free_entities_.size() > 0) {
        entity = free_entities_.front();
        free_entities_.pop_front();
    } else
        entity = next_entity_++;
    // Double the size of the manager if capacity is reached.
    if (entity >= entities_.size())
        Resize(versions_.capacity() * 2);
    assert(entity < entities_.size() &&
           "Created entity is outside of manager entity vector range");
    assert(!entities_[entity] &&
           "Cannot create new entity from live entity");
    assert(!refresh_[entity] &&
           "Cannot create new entity from refresh marked entity");
    // Mark entity for refresh.
    refresh_[entity] = true;
    refresh_required_ = true;
    // Entity version incremented here.
    return Entity{ entity, ++versions_[entity], this };
}

template <typename ...Ts>
inline Entity Manager::CopyEntity(const Entity& e) {
    // Create new entity in the manager to copy to.
    // TODO: Consider making this creation optional as sometimes it
    // is more intuitive to create the entity outside this function.
    Entity copy_entity{ CreateEntity() };
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
        for (auto pool : pools_)
            if (pool != nullptr && pool->Has(from))
                pool->Copy(from, to);
    return copy_entity;
}

inline void Manager::Refresh() {
    if (refresh_required_) {
        // This must be set before refresh starts in case
        // events are called (for instance during entity deletion).
        refresh_required_ = false;
        assert(entities_.size() == versions_.size() &&
               "Refresh failed due to varying entity vector and version vector size");
        assert(entities_.size() == refresh_.size() &&
               "Refresh failed due to varying entity vector and refresh vector size");
        assert(next_entity_ <= entities_.size() &&
               "Next available entity must not be out of bounds of entity vector");
        impl::Index alive{ 0 };
        impl::Index dead{ 0 };
        for (impl::Index entity{ 0 }; entity < next_entity_; ++entity) {
            // Entity was marked for refresh.
            if (refresh_[entity]) {
                refresh_[entity] = false;
                if (entities_[entity]) { // Marked for deletion.
                    ClearEntity(entity);
                    entities_[entity] = false;
                    ++versions_[entity];
                    free_entities_.emplace_back(entity);
                    ++dead;
                } else { // Marked for 'creation'.
                    entities_[entity] = true;
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

inline void Manager::Reserve(std::size_t capacity) {
    entities_.reserve(capacity);
    refresh_.reserve(capacity);
    versions_.reserve(capacity);
    assert(entities_.capacity() == refresh_.capacity() &&
           "Entity and refresh vectors must have the same capacity");
}

inline void Manager::Resize(std::size_t size) {
    if (size > entities_.size()) {
        entities_.resize(size, false);
        refresh_.resize(size, false);
        versions_.resize(size, impl::null_version);
    }
    assert(entities_.size() == versions_.size() &&
           "Resize failed due to varying entity vector and version vector size");
    assert(entities_.size() == refresh_.size() &&
           "Resize failed due to varying entity vector and refresh vector size");
}

inline void Manager::Clear() {
    count_ = 0;
    next_entity_ = 0;
    refresh_required_ = false;

    entities_.clear();
    refresh_.clear();
    versions_.clear();
    free_entities_.clear();

    for (auto pool : pools_) {
        if (pool != nullptr) {
            pool->Clear();
        }
    }
}

inline void Manager::DestroyPools() {
    for (auto& pool : pools_) {
        delete pool;
        pool = nullptr;
    }
}

inline void Manager::Reset() {
    Clear();

    entities_.shrink_to_fit();
    refresh_.shrink_to_fit();
    versions_.shrink_to_fit();
    free_entities_.shrink_to_fit();

    DestroyPools();
    pools_.clear();
    pools_.shrink_to_fit();

    Reserve(1);
}

inline void Manager::DestroyEntity(impl::Index entity, impl::Version version) {
    assert(entity < versions_.size());
    assert(entity < refresh_.size());
    if (versions_[entity] == version) {
        if (entities_[entity]) {
            refresh_[entity] = true;
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
            refresh_[entity] = false;
            ++versions_[entity];
            free_entities_.emplace_back(entity);
        }
    }
}

inline bool Manager::Match(impl::Index entity1, impl::Index entity2) {
    for (auto pool : pools_)
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

inline std::size_t Manager::Size() const {
    return count_;
}

inline std::size_t Manager::Capacity() const {
    return versions_.capacity();
}

template <typename T>
inline void Manager::ForEachEntity(T function) {
    assert(entities_.size() == versions_.size() &&
           "Cannot loop through manager entities if and version and entity vectors differ in size");
    assert(next_entity_ <= entities_.size() &&
           "Last entity must be within entity vector range");
    for (impl::Index entity{ 0 }; entity < next_entity_; ++entity)
        if (entities_[entity])
            function(Entity{ entity, versions_[entity], this });
}

template <typename ...Ts, typename T>
inline void Manager::ForEachEntityWith(T function) {
    static_assert(sizeof ...(Ts) > 0,
                  "Cannot loop through each entity without providing at least one component type");
    assert(entities_.size() == versions_.size() &&
           "Cannot loop through manager entities if and version and entity vectors differ in size");
    assert(next_entity_ <= entities_.size() &&
           "Last entity must be within entity vector range");
    auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
    // Check that none of the requested component pools are nullptrs.
    if (((std::get<impl::Pool<Ts>*>(pools) != nullptr) && ...))
        for (impl::Index entity{ 0 }; entity < next_entity_; ++entity)
            // If entity is alive and has the components, call lambda on it.
            if (entities_[entity] && (std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Has(entity) && ...))
                    function(Entity{ entity, versions_[entity], this },
                             (std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Get(entity))...);
}

template <typename ...Ts, typename T>
inline void Manager::ForEachEntityWithout(T function) {
    assert(entities_.size() == versions_.size() &&
           "Cannot loop through manager entities if and version and entity vectors differ in size");
    assert(next_entity_ <= entities_.size() &&
           "Last entity must be within entity vector range");
    auto pools{ std::make_tuple(GetPool<Ts>(GetId<Ts>())...) };
    // Check that none of the requested component pools are nullptrs.
    if (((std::get<impl::Pool<Ts>*>(pools) != nullptr) && ...))
        for (impl::Index entity{ 0 }; entity < next_entity_; ++entity)
            // If entity is alive and does not have one of the components, call lambda on it.
            if (entities_[entity] && (!std::get<impl::Pool<Ts>*>(pools)->impl::template Pool<Ts>::Has(entity) || ...))
                function(Entity{ entity, versions_[entity], this });
}

template <typename T, typename ...Ts>
inline T& Entity::Add(Ts&&... constructor_args) {
    assert(IsAlive() && "Cannot add component to dead or null entity");
    return manager_->Add<T>(entity_, manager_->GetId<T>(), std::forward<Ts>(constructor_args)...);
}

template <typename ...Ts>
inline void Entity::Remove() {
    assert(IsAlive() && "Cannot remove component(s) from dead or null entity");
    (manager_->Remove<Ts>(entity_, manager_->GetId<Ts>()), ...);
}

template <typename ...Ts>
inline bool Entity::Has() const {
    assert(IsAlive() && "Cannot check if dead or null entity has component(s)");
    return IsAlive() && (manager_->Has<Ts>(entity_, manager_->GetId<Ts>()) && ...);
}

template <typename ...Ts>
inline bool Entity::HasAny() const {
    assert(IsAlive() && "Cannot check if dead or null entity has any component(s)");
    return IsAlive() && (manager_->Has<Ts>(entity_, manager_->GetId<Ts>()) || ...);
}

template <typename ...Ts>
inline decltype(auto) Entity::Get() const {
    assert(IsAlive() && "Cannot get component(s) from dead or null entity");
    return manager_->Get<Ts...>(entity_);
}

template <typename ...Ts>
inline decltype(auto) Entity::Get() {
    assert(IsAlive() && "Cannot get component(s) from dead or null entity");
    return manager_->Get<Ts...>(entity_);
}

inline void Entity::Clear() {
    assert(IsAlive() && "Cannot clear components of dead or null entity");
    manager_->ClearEntity(entity_);
}

inline bool Entity::IsAlive() const {
    return manager_ != nullptr && manager_->IsAlive(entity_, version_);
}

inline void Entity::Destroy() {
    if (IsAlive())
        manager_->DestroyEntity(entity_, version_);
}

inline const Manager& Entity::GetManager() const {
    assert(manager_ != nullptr && "Cannot return parent manager of a null entity");
    return *manager_;
}

inline Manager& Entity::GetManager() {
    return const_cast<Manager&>(static_cast<const Entity&>(*this).GetManager());
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
        manager_->Match(entity_, e.entity_));
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

} // namespace ecs

namespace std {

template <>
struct hash<ecs::Entity> {
    std::size_t operator()(const ecs::Entity& e) const {
        // Source: https://stackoverflow.com/a/17017281
        std::size_t hash{ 17 };
        hash = hash * 31 + std::hash<ecs::Manager*>()(e.manager_);
        hash = hash * 31 + std::hash<ecs::impl::Index>()(e.entity_);
        hash = hash * 31 + std::hash<ecs::impl::Version>()(e.version_);
        return hash;
    }
};

} // namespace std
