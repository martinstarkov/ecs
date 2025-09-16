# C++17 Entity Component System Library

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Usage](#usage)
- [CMake Integration](#cmake-integration)
- [Manager](#manager)
- [Entities](#entities)
- [Components](#components)
- [Systems](#systems)
- [Manager Utility Functions](#manager-utility-functions)
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

When you create an entity, you can add components to it, but the entity won't be considered "alive" by the manager until you call **`manager.Refresh()`**. This means the entity won't show up in entity iteration functions like `manager.Entities()` or `manager.EntitiesWith<T>()`. This behavior prevents iterator invalidation if entities are created or destroyed while looping through them—such as when destroying entities with an expired lifetime component.

### Example

```cpp
auto entity = manager.CreateEntity();
entity.Add<LifetimeComponent>(5.0f);  // Add a component to the entity

// Entity is not yet considered "alive" for iteration
for (auto e : manager.Entities()) {
    // This loop will not include the newly created entity yet
}

// Now call Refresh to update the manager's internal state
manager.Refresh();  // The entity is now considered "alive" and will be included

// Destroy the entity
entity.Destroy();

// The entity is still considered alive until we call Refresh
for (auto e : manager.Entities()) {
    // The entity will still be part of the iteration, because it hasn't been refreshed yet
}

// Call Refresh again to update the state, removing destroyed entities
manager.Refresh();

// Now the entity is excluded from future entity loops
```

In this example, the entity is only considered "alive" in the manager and part of the iteration after calling `manager.Refresh()`. Similarly, destroyed entities won't be removed from the loop until `Refresh()` is called again, ensuring there is no iterator invalidation while modifying the entity list.

### Invalid Entities

To represent an invalid entity, you can use a default-constructed `ecs::Entity{}`.

```cpp
ecs::Entity invalid_entity{}; // Using default-constructed entity
```

### Entity Comparisons & Identity

- Use `==` and `!=` to compare entity handles.
- Use `entity.IsIdenticalTo(other)` to compare entity components (not just handles).

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

### Checking for Components

```cpp
bool is_human = entity.Has<HumanComponent>();
bool is_cyborg = entity.Has<HumanComponent, RobotComponent>();
```

### Retrieving Components

```cpp
auto& human = entity.Get<HumanComponent>();
auto [robot, alien] = entity.Get<RobotComponent, AlienComponent>();
```

> ⚠️ Accessing a missing component triggers a debug assertion. Advisable to check with `Has<T>()` first.

### Removing Components

```cpp
entity.Remove<HumanComponent>();
entity.Remove<RobotComponent, AlienComponent>();
```

No-op if the component doesn't exist.

---

## Systems

Systems are lambdas or loops that act on entities with specific component combinations.

### All Entities

```cpp
for (auto entity : manager.Entities()) {
    entity.Add<ZombieComponent>();
}
```

### Entities With Components

```cpp
for (auto [entity, zombie, food] : manager.EntitiesWith<ZombieComponent, FoodComponent>()) {
    if (food.amount < 10) {
        // feed the zombie
    }
}
```

### Entities Without Components

```cpp
for (auto entity : manager.EntitiesWithout<FoodComponent, HungerShieldComponent>()) {
    entity.Destroy();
}
manager.Refresh();
```

---

## Manager Utility Functions

### Copying Entities

```cpp
auto new_entity = entity.Copy();
// Or:
auto new_entity = manager.CopyEntity(entity);
// Or copy to existing:
manager.CopyEntity(source_entity, destination_entity);
```

You can also copy specific components:

```cpp
auto new_entity = manager.CopyEntity<FoodComponent, HealthComponent>(entity);
```

### Cloning Managers

```cpp
auto new_manager = manager.Clone();
assert(new_manager == manager); // Deep comparison
```

### Introspection

```cpp
bool is_empty = manager.IsEmpty();
std::size_t size = manager.Size();
std::size_t capacity = manager.Capacity();
```

### Capacity Management

```cpp
manager.Reserve(100); // Preallocate for 100 entities
manager.Clear();      // Remove all entities, keep capacity
manager.Reset();      // Remove all entities and free memory
```

---

## Acknowledgements

Thanks to the following for their ideas and inspiration:

- **[Vittorio Romeo](https://github.com/SuperV1234)** — [CppCon 2015 ECS talk](https://www.youtube.com/watch?v=NTWSeQtHZ9M)
- **[Michele Caini (skypjack)](https://github.com/skypjack)** — [ECS blog series](https://skypjack.github.io/2019-02-14-ecs-baf-part-1/)
- **[Adam (T-Machine)](http://t-machine.org/)** — [Article on data structures for ECS](http://t-machine.org/index.php/2014/03/08/data-structures-for-entity-systems-contiguous-memory/)

---

> If you find this library useful or have suggestions, please feel free to contribute or open issues on GitHub!
