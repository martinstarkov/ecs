# C++ Single-Header Entity Component System Library

## Introduction
This library aims to be as cache-friendly as possible while also supporting runtime addition of new component types. This is achieved by storing all components contiguously in memory in the form of void pointers, which are then cast to specific components when required. As far as this approach goes, while it is not type safe, the implementation gets around this by maintaining an indexing table of components with unique type ids for each entity which prevents retrieval / access of invalid component types.


## Usage

Simply add `#include "ECS.h"` in whichever file utilizes the entity component system.


## Manager

The core storage unit of entities and systems is the `ecs::Manager` class, created like so.
```c++
#include "ECS.h"

ecs::Manager my_manager;
```
Note that managers can be moved, cannot be copied, but can be cloned. This prevents accidental copies as they are expensive and some users may not used to move semantics.

Manager functions are elaborated upon at the end of this documentation.


## Entities

Users can interact with entities through handles (`ecs::Entity`), which can be created using the manager.
```c++
ecs::Manager my_manager;

auto entity1 = my_manager.CreateEntity();
```
Important note: Calling ```CreateEntity()``` will give the user a handle which can be used as normal but the entity will not me considered a part of the manager until a refresh is called. The memory associated with this entity is, however, stored in the manager and therefore any added components will be stored in the manager.
```c++
my_manager.Refresh();
```

Entities can be marked for destruction using the handle function.
```c++
entity1.Destroy();
```
Important note: Similarly to creation, an entity and its components are not immediately destroyed when ```Destroy()``` is called, but rather it flags the entity and its components to be destroyed on the next manager refresh call. This allows for loops where entities may destroy other entities to finish before invalidating themselves.

TLDR: Call ```Refresh()``` after ```CreateEntity()``` or ```entity.Destroy()``` unless you want to wait for the entity to be considered 'valid'. Valid means it appears in system caches.

A null entity can be created using `ecs::null`, this may be useful if attempting to find an entity which matches a specific condition and returning an invalid entity if no such entity is found.

Note that ```ecs::null``` is technically an instance of the ```ecs::NullEntity``` class, therefore the auto keyword cannot be used if you wish to set ecs::null entity to something else later.
```c++
ecs::Entity initially_invalid_entity = ecs::null;
// vs.
auto always_invalid_entity = ecs::null;
```

Entity handles contain some properties / functions which may be useful in specific circumstances:
- Overloaded `==` and `!=` operators for comparison between entities, for `==` comparison to return true the manager, version, and id of both entities must match.
- Comparison with `ecs::null` (invalid entity).
- Custom hashing function for use as keys in unordered maps or sets.
- `GetManager()` returns a reference to the parent manager if it exists.


## Components

Components contain all the data but none of the logic.
Components can be defined without inheriting from a base class. Due to runtime addition support, the manager does not need to be notified of new component additions in compile time.

Every component requires a valid constructor, destructor, and must be move constructable.
```c++
struct HumanComponent {
    HumanComponent(int age, double height) : age{ age }, height{ height } {}
    int age;
    double height;
}
```

The user can interact with an entity's components through the entity handle.

`AddComponent<>(constructor_args...)` requires you to pass the component type as a template parameter and the constructor arguments as parameters. If the entity already contains the component type, it will be replaced.
A reference to the newly created component is returned.
```c++
auto entity1 = my_manager.CreateEntity();

int age = 22;

double height = 180.5;

auto& human_component = entity1.AddComponent<HumanComponent>(age, height);

human_component.height += 0.5;
```

Checking if an entity has a component can be done using `HasComponent<>()`.
```c++
bool is_human = entity1.HasComponent<HumanComponent>();
```
Or for multiple checks at once with `HasComponents<>()`.
```c++
bool has_both = entity1.HasComponents<HumanComponent, OtherComponent>();
```

`GetComponent<>()` returns a reference to the component. If the entity does not have the component a debug assertion will be called.
```c++
auto& human_component = entity1.GetComponent<HumanComponent>();

human_component.age += 1;
```
And when retrieving multiple components at once with `GetComponents<>()`, a tuple is returned.
```c++
auto tuple_of_components = entity1.GetComponents<HumanComponent, OtherComponent>();
```
The components can be accessed easily via a structured binding.
```c++
auto [human_component, other_component] = tuple_of_components;
```

Removing components is easy, simply call `RemoveComponent<>()`.
```c++
entity1.RemoveComponent<HumanComponent>();
```
Or for multiple removals at once `RemoveComponents<>()`.
```c++
entity1.RemoveComponents<HumanComponent, OtherComponent>();
```

## Systems

Systems contain only logic and no data (other than an internal cache of entities).
Systems cache entities with matching components. When declaring a system, one must inherit from the templated `ecs::System<>` base class and pass the components which each entity in the system must contain. If no types are passed then the system will maintain a cache of all manager entities.
The system class provides a virtual `Update()` function which can be overridden and called later via the manager.

`ecs::System` contains a cached variable called `entities` which is a vector of tuples containing an entity handle followed by references to the required components.
The `entities` vector can be looped through using structured bindings and automatically adds / removes relevant entities when their component makeup changes.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> {
    void Update() {
        // The order of the tuple always starts with an entity handle, followed by the template parameters.
        for (auto [entity_handle, human_component, other_component] : entities) {
        
            // ... System logic here ....
            
            // For optional components, the following check will suffice.
            if (entity_handle.HasComponent<OptionalComponent>() {
                auto& optional_component = entity_handle.GetComponent<OptionalComponent>();
                
                // ... More logic here ...
            }
        }
    }
}
```
If components are removed from an entity during the for-loop, the cache will be update accordingly after the `Update()` function has finished. This means that entities changed during the loop will still exist in the `entities` vector until the end of the `Update()` call.
As mentioned previously, if a system destroys entitites, the user must call ```Refresh()``` on the manager in order for the manager to destruct the entities and their respective components.

Systems can be registered with the manager as follows.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> { ... };

ecs::Manager my_manager;

my_manager.AddSystem<MySystem>();
```
Note that since systems exist purely for operating on components with logic, they should not have member variables. For this reason their addition to the manager only supports default construction (no additional arguments).

And updated using the manager.
```c++
my_manager.UpdateSystem<MySystem>();
```
inside your game loop / wherever the manager exists.

Systems can be removed from the manager using `RemoveSystem<>()` which will prevent their `Update<>()` call from passing.
```c++
my_manager.RemoveSystem<MySystem>();

my_manager.UpdateSystem<MySystem>(); // debug assertion called, system has not been added to the manager.
```
In order to get around this one may use `HasSystem<>()`.
```c++
if (my_manager.HasSystem<MySystem>()) {
    my_manager.UpdateSystem<MySystem>();
}
```

Additionally, one may be interested in accessing the parent manager from within the system class (perhaps to create additional entities). This can be done via the inherited `GetManager()` function which returns a reference to the parent manager.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> {
  void Update() {
      auto& manager = GetManager();
      auto new_entity = manager.CreateEntity();
  }
}
```


## Useful manager utility functions

The manager supports a host of useful functions for manipulating specific entities.

One may wish to access all live entities in the manager.
```c++
auto vector_of_entities = my_manager.GetEntities();
```
Or only entities with given components.
```c++
auto vector_of_human_entities = my_manager.GetEntitiesWith<HumanComponent, LifeComponent>();
```
Or without given components.
```c++
auto vector_of_aliens = my_manager.GetEntitiesWithout<HumanComponent>();
```
The number of live entities in the manager can also be found.
```c++
auto entity_count = my_manager.GetEntityCount();
```
Similarly, the number of dead entities in the manager.
```c++
auto dead_entity_count = my_manager.GetDeadEntityCount();
```
Clearing the manager of all entities and components is as easy as.
Note that this maintains the manager entity capacity.
```c++
my_manager.Clear();
```
The manager can be reset fully (equivalent to calling destructor and constructor again) with the following.
```c++
my_manager.Reset();
```
One can also reserve capacity for entities in the manager by passing the desired capacity like so.
```c++
// This reserves space for at least 5 entities ahead of time (possibly saving on allocations).
my_manager.Reserve(5);
```
A manager can be cloned to create an identical manager with equivalent entity and component composition using.
```c++
auto new_manager = my_manager.Clone();
```
All entities can be flagged for destruction with.
```c++
my_manager.DestroyEntities();
```
For more specific destruction, use.
```c++
my_manager.DestroyEntitiesWith<HumanComponent, OtherComponent>();
my_manager.DestroyEntitiesWithout<HumanComponent, OtherComponent>();
```
`GetEntityComponents<>()` returns a vector of tuples where the first tuple element is the entity handle and the rest are references to the components in the order of the template parameter list. This is equivalent to the `entities` variable used in systems.
```c++
auto vector_of_tuples = my_manager.GetEntityComponents<HumanComponent, OtherComponent>();
```
Which can be cycled through easily using a structured binding as before.
```c++
for (auto [entity_handle, human_component, other_component] : vectors_of_tuples) {
    // ... Code here ...
}
```


## Thanks to

Vittorio Romeo ([SuperV1234](https://github.com/SuperV1234/)) for his [brilliant talk at CppCon 2015](https://www.youtube.com/watch?v=NTWSeQtHZ9M) on entity component systems, which provided lots of useful ideas for my implementation.

Michele Caini ([skypjack](https://github.com/skypjack/)) for his [series of articles](https://skypjack.github.io/2019-02-14-ecs-baf-part-1/) on efficient entity components systems.

Adam ([T-Machine](http://t-machine.org/)) for his article on [data structures for entity systems](http://t-machine.org/index.php/2014/03/08/data-structures-for-entity-systems-contiguous-memory/), which inspired me to implement my own component pool memory allocator.
