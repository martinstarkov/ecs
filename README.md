# C++ Entity Component System Library

## Introduction
This library aims to be cache-friendly by storing components contiguously in memory, while also supporting runtime addition of new component types. The implementation uses a dense and sparse set combined with an indexing table to match entities to their respective components.

## Usage

1. Acquire the ```ecs.h``` file from this repository.

2. Add `#include "path/to/ecs.h"` in files which utilize the entity component system.

## Manager

The core storage unit of entities and components is the `ecs::Manager` class, created like so:

```c++
#include "ecs.h"

ecs::Manager manager;
```

Managers can be moved but not copied. This prevents accidental copying as it is expensive and some users may not be used to move semantics. ```manager.Clone()``` should be used if the user wishes to duplicate a manager.

Manager functions are explained at the end of this documentation.


## Entities

Users can interact with entities through `ecs::Entity` handle objects, which are returned by the manager.

```c++
ecs::Manager manager;

auto entity = manager.CreateEntity();
```

Calling ```manager.CreateEntity()``` will create an entity but it will not be considered alive until the manager is refreshed. Entities must be alive to be detected in the manager. The memory associated with the entity is, however, stored in the manager and therefore components can be added or removed to/from the entity before refreshing.

```c++
manager.Refresh();
```

Entities can be marked for destruction using:

```c++
entity.Destroy();
```

But will not be removed from the manager until a refresh is called. This prevents iterator invalidation if entities destroy each other during container loops.
```entity.IsAlive()``` can be called to check the state of validity of an entity in its parent manager.

**TLDR;** Remember to always call ```manager.Refresh()``` after an entity is created or destroyed.

A null (invalid) entity can be represented using `ecs::null`.

Note: ```ecs::null``` is a constexpr instance of the ```ecs::impl::NullEntity``` class, therefore the auto keyword should not be used if the entity will be set to a valid one later.

```c++
ecs::Entity initially_invalid_entity = ecs::null;
// vs.
auto permanently_invalid_entity = ecs::null;
```

Comparing entities can be done as follows:
- Using overloaded ```==``` or ```!=``` operator to compare two entity handles (or ```ecs::null```).
- Using ```entity.IsIdenticalTo(other_entity)``` to compare entity composition (whether they have identical components).

Entity handles implement ```std::hash``` which allows them to be used as keys in hashed containers such as ```std::unordered_map```.

```entity.GetManager()``` returns a reference to the entity handle's parent manager.

## Components

Components can be viewed as properties (or data) of an entity. Due to runtime addition support, the manager does not need to be notified of new component types in compile time. 

Components require a valid constructor, destructor, and move constructor.

```c++
struct HumanComponent {
    HumanComponent(int age, double height) : age{ age }, height{ height } {}
    int age;
    double height;
};
```

The user can interact with an entity's components through the entity handle.

`AddComponent<Type>(constructor_args...)` requires you to pass the component type as a template parameter and the component constructor arguments as function parameters. If the entity already has the component type, it will be replaced.
A reference to the newly created component is returned.

```c++
auto entity = manager.CreateEntity();

int age = 22;
double height = 180.5;

auto& human = entity.AddComponent<HumanComponent>(age, height);
human.height += 0.5;
```

Checking if an entity has component(s) can be done like so:

```c++
bool is_human = entity.HasComponent<HumanComponent>();

bool is_cyborg = entity.HasComponents<HumanComponent, RobotComponent>();
```

Component(s) references can be retrieved from an entity using:

```c++
auto& human = entity.GetComponent<HumanComponent>();

auto [robot, alien] = entity.GetComponents<RobotComponent, AlienComponent>();

human.age += 1;
```

If an entity does not have the requested component, a debug assertion is called. It is therefore advisable to wrap component retrieval in an if-statement such as:

```c++
if (entity.HasComponent<HumanComponent>()) {
    auto& human = entity.GetComponent<HumanComponent>();
}
```

Components can be removed using:

```c++
entity.RemoveComponent<HumanComponent>();

entity.RemoveComponents<RobotComponent, AlienComponent>();
```

Nothing happens if the entity did not have the component type.

## Systems

Systems represent the logic of how grouped entities are manipulated. In this implementation, they are simply lambdas whose arguments are filled with entity handles and components.

For instance, each entity can be looped through:

```c++
manager.ForEachEntity([](ecs::Entity entity) {
    entity.AddComponent<ZombieComponent>();
    entity.AddComponent<FoodComponent>();
});
```

Or only entities with specific components:

```c++
manager.ForEachEntityWith<ZombieComponent, FoodComponent>(
    [&](ecs::Entity entity, auto& zombie, auto& food) {
    if (food.amount < threshold) {
        // ...
    }
});
```

Or only entities without specific components:

```c++
manager.ForEachEntityWithout<FoodComponent>([&](ecs::Entity entity) {
    entity.Destroy();
});
manager.Refresh();
```

## Manager utility functions

As copying entity handles does not create new entities, copying all of an entity's components to a new entity can be done via the manager:

```c++
auto new_entity = manager.CopyEntity(entity);

assert(new_entity.IsIdenticalTo(entity)); // passes

manager.Refresh() // still required as with CreateEntity()
// new_entity now detected when looping through manager entities. 
```

This requires that all of the entity's components are copy-constructible. If the user wishes to copy only certain components they can do so using template parameters:

```c++
auto new_entity = manager.CopyEntity<ZombieComponent, FoodComponent>(entity);

// new_entity now has only the same ZombieComponent and FoodComponent as entity.
assert(!new_entity.IsIdenticalTo(entity)); // passes
``` 

The number of alive entities in the manager can be found using:

```c++
std::size_t entity_count = manager.GetEntityCount();
```

Destroying all of the entities and components in a manager is similar to standard library containers:

```c++
manager.Clear();
```

Note that this maintains the manager entity capacity.

The manager can be reset fully (including capacity) using:

```c++
manager.Reset();
```

The user can reserve entity capacity in advance with:

```c++
manager.Reserve(5); // 5 entities.
```

As mentioned previously, managers can only be duplicated like so:

```c++
auto new_manager = manager.Clone();

// manager comparison is slow and inadvisable for large managers.
assert(new_manager == manager); 
```


## Thanks to

Vittorio Romeo ([SuperV1234](https://github.com/SuperV1234/)) for his [brilliant talk at CppCon 2015](https://www.youtube.com/watch?v=NTWSeQtHZ9M) on entity component systems, which provided lots of useful ideas for my implementation.

Michele Caini ([skypjack](https://github.com/skypjack/)) for his [series of articles](https://skypjack.github.io/2019-02-14-ecs-baf-part-1/) on efficient entity component systems.

Adam ([T-Machine](http://t-machine.org/)) for his article on [data structures for entity systems](http://t-machine.org/index.php/2014/03/08/data-structures-for-entity-systems-contiguous-memory/), which inspired me to implement my own component pool allocator.
