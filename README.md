# C++17 Entity Component System Library

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Usage](#usage)
- [CMake Integration](#cmake-integration)
- [Manager](#manager)
- [Entities](#entities)
- [Components](#components)
- [Views](#views)
- [Hooks](#hooks)
- [Manager Utility Functions](#manager-utility-functions)
- [Serialization (Advanced)](#serialization-advanced)
- [Acknowledgements](#acknowledgements)

---

## Introduction

This C++ Entity Component System (ECS) library is designed to be **cache-friendly** by storing components contiguously in memory and supports **runtime addition** of new component types. It uses a hybrid of **dense and sparse sets** along with an **indexing table** to efficiently associate entities with their components.

---

## Quick Start

```cpp
#include "ecs/ecs.h"

struct Position {
    float x, y;
};

struct Velocity {
    float dx, dy;
};

int main() {
    ecs::Manager manager;

    auto entity = manager.CreateEntity();
    entity.Add<Position>(0.f, 0.f);
    entity.Add<Velocity>(1.f, 1.f);

    manager.Refresh();

    for (auto [e, pos, vel] : manager.EntitiesWith<Position, Velocity>()) {
        pos.x += vel.dx;
        pos.y += vel.dy;
    }
}
```

---

## Usage

1. Download or include the `include/ecs/ecs.h` file from this repository manually or via CMake.
2. Add `#include "ecs/ecs.h"` in your C++ source files that use the ECS library.

---

## CMake Integration

Add the following to your `CMakeLists.txt` (minimum version: 3.14) to fetch and include the ECS library:

```cmake
include(FetchContent)

FetchContent_Declare(
    ecs
    GIT_REPOSITORY https://github.com/martinstarkov/ecs.git
    GIT_TAG main
)
FetchContent_MakeAvailable(ecs)

set(ECS_INCLUDE_DIR "${ecs_SOURCE_DIR}/include")

target_include_directories(<target_name> PRIVATE ${ECS_INCLUDE_DIR})
```

Replace `<target_name>` with your actual CMake target.

---

## Manager

The `ecs::Manager` is the core class responsible for storing and managing all entities and their components.

```cpp
#include "ecs/ecs.h"

ecs::Manager manager;
```

---

## Entities

Entities are lightweight handles to component data stored in the manager.

```cpp
auto entity = manager.CreateEntity();
```

### Entity Lifecycle and Manager Refresh

When you create an entity, you can add components to it, but the entity won't be considered "alive" by the manager until you call **`manager.Refresh()`**. This means the entity won't show up in entity iteration functions like `manager.Entities()` or `manager.EntitiesWith<T>()`. This behavior prevents iterator invalidation if entities are created or destroyed while looping through them, such as when destroying entities with an expired lifetime component.

### Example

```cpp
auto entity = manager.CreateEntity();
entity.Add<LifetimeComponent>(5.0f);  // add a component to the entity

// entity is not yet considered "alive" for iteration
for (auto e : manager.Entities()) {
    // this loop will not include the newly created entity yet
}

// now call Refresh to update the manager's internal state
manager.Refresh();  // the entity is now considered "alive" and will be included

// destroy the entity
entity.Destroy();

// the entity is still considered alive until we call Refresh
for (auto e : manager.Entities()) {
    // the entity will still be part of the iteration, because it hasn't been refreshed yet
}

// call Refresh again to update the state, removing destroyed entities
manager.Refresh();

// now the entity is excluded from future entity loops
```

In this example, the entity is only considered "alive" in the manager and part of the iteration after calling `manager.Refresh()`. Similarly, destroyed entities won't be removed from the loop until `Refresh()` is called again, ensuring there is no iterator invalidation while modifying the entity list.

### Null Entities

To represent a null entity, you can use a default-constructed `ecs::Entity{}`.


```cpp
ecs::Entity null_entity{}; // using default-constructed entity
```

### Entity Comparisons & Identity

- Use `==` and `!=` to compare entity handles.
- Use `entity.IsIdenticalTo(other)` to compare entity components (not just handles).
- `Entity::operator bool()` returns true only if the entity is **alive**.

Entity handles are hashable and usable in `std::unordered_map`.

### Accessing the Entity's Parent Manager

You can retrieve the parent manager of an entity using the `GetManager()` function. This gives you a reference to the manager that owns the entity.

```cpp
auto& manager_ref = entity.GetManager();
```

This allows one to interact with or manipulate the manager directly from the entity handle.

---

## Components

Components represent the data or properties of an entity. They are stored in contiguous memory, which can improve cache efficiency. Components must have a valid constructor, destructor, and move constructor.

```cpp
struct HumanComponent {
    HumanComponent(int age, double height) : age{ age }, height{ height } {}
    int age;
    double height;
};
```

### Adding Components

```cpp
auto& human = entity.Add<HumanComponent>(22, 180.5);
human.height += 0.5;
```

Adding a component replaces the existing one if it already exists.

> ⚠️ **Warning**: Adding a component to an entity may invalidate previously saved references to other entity components of the same type. This is because adding a new component to the contiguous container may cause it to expand or be relocated in memory.

```cpp
auto& pos = entity.TryAdd<Position>(0.f, 0.f);
```

Try add only adds a component if the entity does not already have it. It returns a reference to the new / existing component.

### Checking for Components

```cpp
bool is_human = entity.Has<HumanComponent>();
bool is_cyborg = entity.Has<HumanComponent, RobotComponent>();
bool is_either = entity.HasAny<HumanComponent, RobotComponent>();
```

### Retrieving Components

```cpp
auto& human = entity.Get<HumanComponent>();
auto [robot, alien] = entity.Get<RobotComponent, AlienComponent>();
```

> ⚠️ Accessing a missing component through `Get<T>()` triggers a debug assertion. Advisable to check with `Has<T>()` first or use `TryGet<T>()`.

```cpp
if (auto* pos = entity.TryGet<Position>()) {
    pos->x += 1.f;
}
```

Returns a component pointer or `nullptr` if the component does not exist.

### Removing Components

```cpp
entity.Remove<HumanComponent>();
entity.Remove<RobotComponent, AlienComponent>();
```

No-op if the component doesn't exist.

---

## Views

Views provide filtered access to entities and optionally their components.

```cpp
for (auto entity : manager.Entities()) {
    // all alive entities
}

for (auto [entity, pos, vel] : manager.EntitiesWith<Position, Velocity>()) {
    // entities that have Position and Velocity
}

for (auto entity : manager.EntitiesWithout<Sleeping>()) {
    // entities missing Sleeping
}
```

`Entities()` yields entity handles.
`EntitiesWith<T...>()` yields `entity, component...`.
`EntitiesWithout<T...>()` yields entity handles.

Modifying components during iteration may affect which entities are yielded later in the loop.
This is safe, but order-dependent (based on entity ID iteration).

### View Utility Functions

Views also provide a few utility helpers in addition to range-based iteration.

```cpp
// save the view
auto view = manager.EntitiesWith<Position, Velocity>();
```

#### Count and Collection

```cpp
std::size_t count = view.Count();
auto entities = view.GetVector();
```

### Accessing Elements

```cpp
// order follows internal entity iteration order
auto entities = view.GetVector();

// these are null entities if the view is empty
auto first = view.Front();
auto last  = view.Back();

```

#### Membership

```cpp
bool contains = view.Contains(entity);
```

#### Range functions

Predicates to range functions can take in either just the entity or the entity together with the view components.

```cpp
bool any_fast = view.AnyOf([](auto entity, const auto& pos, const auto& vel) {
    return vel.dx != 0.0f || vel.dy != 0.0f;
});

bool all_valid = view.AllOf([](auto entity) {
    return entity.IsAlive();
});

std::size_t moving = view.CountIf([](auto entity, const auto& pos, const auto& vel) {
    return vel.dx != 0.0f || vel.dy != 0.0f;
});

// returns null entity if nothing matches
auto found = view.FindIf([](auto entity, const auto& pos) {
    return pos.x > 100.f;
});
```

#### ForEach

```cpp
view.ForEach([](auto entity, auto& pos, auto& vel) {
    pos.x += vel.dx;
    pos.y += vel.dy;
});
```

#### Transform

`Transform` projects the view into a `std::vector` of values.

```cpp
auto ids = view.Transform([](auto entity) {
    return entity.GetId();
});

// std::vector<result>, result is deduced from the function return type
auto speeds = view.Transform([](auto entity, const auto& pos, const auto& vel) {
    return vel.dx * vel.dx + vel.dy * vel.dy;
});
```

---

## Hooks

Hooks allow you to react to component lifecycle events.

### Registering Hooks

```cpp
auto& hook = manager.OnConstruct<Position>();
hook.Connect<&OnPositionCreated>();
```

You can register hooks for:

* `OnConstruct<T>()`
* `OnDestruct<T>()`
* `OnUpdate<T>()`

---

### Hook Types

#### Free function

```cpp
void OnPositionCreated(ecs::Entity e) {}

manager.OnConstruct<Position>().Connect<&OnPositionCreated>();
```

#### Member function

```cpp
struct System {
    void OnUpdate(ecs::Entity e) {}
};

System sys;
manager.OnUpdate<Position>().Connect<System, &System::OnUpdate>(&sys);
```

#### Lambda (non-capturing only)

```cpp
manager.OnDestruct<Position>().Connect([](ecs::Entity e) {
    // your cleanup logic
});
```

---

### Removing Hooks

```cpp
manager.RemoveOnConstruct<Position>(hook);
```

You must keep the returned hook instance if you wish to remove it later.

---

### Checking Hooks

```cpp
if (manager.HasOnUpdate<Position>(hook)) {
    // hook is registered
}
```

---

### When Hooks Fire

| Event     | Trigger                                             |
| --------- | --------------------------------------------------- |
| Construct | `Add<T>()` (new component)                          |
| Update    | `Add<T>()` (replace existing) or manual `entity.Update<T>()`        |
| Destruct  | `Remove<T>()`, entity destruction, or manager reset |

> ⚠️ The update hook does not fire if the user changes a component directly through a reference. One solution is to provide set/get functions and manually trigger the hook via `entity.Update<T>()`.

---

## Manager Utility Functions

### Copying Entities

```cpp
auto new_entity = entity.Copy();
// or:
auto new_entity = manager.CopyEntity(entity);
// or copy to existing:
manager.CopyEntity(source_entity, destination_entity);
```

You can also copy specific components:

```cpp
auto new_entity = manager.CopyEntity<FoodComponent, HealthComponent>(entity);
```

### Copying and Comparing Managers

```cpp
auto new_manager = manager; // copy constructs a new manager (expensive)
// Note: operator== compares identity (same instance), not deep equality
assert(new_manager != manager);
```

### Introspection

```cpp
bool is_empty = manager.IsEmpty();
std::size_t size = manager.Size();
std::size_t capacity = manager.Capacity();
```

### Capacity Management

```cpp
manager.Reserve(100); // preallocate for 100 entities
manager.Clear();      // remove all entities, keep capacity
manager.Reset();      // remove all entities and free memory
```

---

## Serialization (Advanced)

The ECS supports passing in custom archivers via `BaseManager<TArchiver>`.

An archiver must implement methods like:

* `WriteComponent<T>()`
* `HasComponent<T>()`
* `ReadComponent<T>()`
* `WriteComponents<T>()`
* `SetDenseSet<T>()`
* `SetSparseSet<T>()`
* `ReadComponents<T>()`
* `GetDenseSet<T>()`
* `GetSparseSet<T>()`

The default `VoidArchiver` disables serialization.

> ⚠️ Components must be default-constructible to support deserialization.

---

## Acknowledgements

Thanks to the following for their ideas and inspiration:

- **[Vittorio Romeo](https://github.com/SuperV1234)** — [CppCon 2015 ECS talk](https://www.youtube.com/watch?v=NTWSeQtHZ9M)
- **[Michele Caini (skypjack)](https://github.com/skypjack)** — [ECS blog series](https://skypjack.github.io/2019-02-14-ecs-baf-part-1/)
- **[Adam (T-Machine)](http://t-machine.org/)** — [Article on data structures for ECS](http://t-machine.org/index.php/2014/03/08/data-structures-for-entity-systems-contiguous-memory/)

---

> If you find this library useful or have suggestions, please feel free to contribute or open issues on GitHub!
