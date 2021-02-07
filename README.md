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
Note that managers cannot be copied so take special care to always use appropriate move semantics / pointers / references when passing managers to functions / objects.


## Entities

Users can interact with entities through handles (`ecs::Entity`), which can be created using the manager.
```c++
ecs::Manager my_manager;

auto entity1 = my_manager.CreateEntity();
```
And destroyed using the handle.
```c++
entity1.Destroy();
```
Important note: Destroying an entity effectively invalidates all handles to that entity by incrementing the internal version counter of that particular entity within the manager. This, however, will not prevent the user from using existing references to entity components (retrieved before destruction of entity) as they remain in memory and are only replaced upon the addition of another component of the same type.

A null entity can be created using `ecs::null`, this may be useful if attempting to find an entity which matches a specific condition and returning an invalid entity if no such entity is found.
```c++
// Note: that ecs::null is technically an id, therefore the auto keyword cannot be used if you wish to set your null entity to something else later.
ecs::Entity my_invalid_entity = ecs::null;
```
Entity validity is elaborated upon in the end of this subsection.

Cycling through multiple entities without the use of systems can be done via the `ForEachEntity(lambda)` manager method, passing it a lambda function.
The lambda's parameter should be an entity handle (`ecs::Entity`) passed by value as it is created directly into the parameter list.
```c++
my_manager.ForEachEntity([] (auto entity_handle) {
    // ... Do stuff with entities here ...
});
```
Or if specific components are required, by populating the template parameter list of `ForEach<>(lambda)`. The lambda's parameters should start with the entity handle as explained above and followed by references to the components in the same order as the template parameter list.
```c++
my_manager.ForEach<HumanComponent, OtherComponent>([] (auto entity_handle, auto& human_component, auto& other_component) {
    // ... Do stuff with entities / components here ...
});
```

Additionally, entity handles contain some properties / functions which may be useful in specific circumstances:
- Overloaded `==` and `!=` operators for comparison between entities, for `==` comparison to return true the manager, version, and id of both entities must match.
- Comparison with `ecs::null` (invalid entity).
- `GetId()` returns the unique entity id. Importantly, the id should not be used for mapping of any kind as ids are reused upon destruction and creation of new entities.
- `GetVersion()` returns the version number of the entity, i.e. how many times the entity id has been reused after destruction.
- `GetManager()` returns a pointer to the parent manager, or nullptr if the entity is `ecs::null`.
- `IsAlive()` and `IsValid()` return bools indicating whether or not the entity is alive (or destroyed) and valid (or `ecs::null`), respectively. 


## Components

Components can be defined without inheriting from a base class. Due to runtime addition support, the manager does not need to be notified of new component additions in compile time.

Every component requires a valid constructor.
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

An important note when using component references is that removing a component while a reference to it exists will not invalidate the reference, but will ensure that no new references to it can be retrieved. 
The memory at the reference location is maintained until a new component of the same type is added to any entity (this will write on top of the old component memory).

Removing components is easy, simply call `RemoveComponent<>()`.
```c++
entity1.RemoveComponent<HumanComponent>();
```
Or for multiple removals at once `RemoveComponents<>()`.
```c++
entity1.RemoveComponents<HumanComponent, OtherComponent>();
```

Replacing a component using `ReplaceComponent<>()` asserts that the component exists. This might be useful if seeking to enforce the component's existence when changing it.
```c++
auto& human_component = entity1.ReplaceComponent<HumanComponent>(23, 176.3);
```


## Systems

Systems cache entities with matching components. When declaring a system, one must inherit from the templated `ecs::System<>` base class and pass the components which each entity in the system must contain.
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
If components are removed from an entity during the for-loop, the cache will be update accordingly after the `Update()` function has finished. This means that entities changed during the loop might still exist in the `entities` vector until the end of the `Update()` call.
As mentioned previously, the component references in the `entities` vector will remain valid and point to the component memory (even after a component is removed) until that memory is overriden by a new component addition. This is a design choice.

Systems can be registered with the manager as follows.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> { ... };

ecs::Manager my_manager;

my_manager.AddSystem<MySystem>();
```
And updated using the manager.
```c++
my_manager.Update<MySystem>();
```
inside your game loop / wherever the manager exists.

Systems can be removed from the manager using `RemoveSystem<>()` which will prevent their `Update<>()` call from passing.
```c++
my_manager.RemoveSystem<MySystem>();

my_manager.Update<MySystem>(); // debug assertion called, system has not been added to the manager.
```
In order to get around this one may use `HasSystem<>()`.
```c++
if (my_manager.HasSystem<MySystem>()) {
    my_manager.Update<MySystem>();
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
Destroying all entities in the manager is as easy as.
```c++
my_manager.Clear();
```
Or identically.
```c++
my_manager.DestroyEntities();
```
For more specific destruction, use.
```c++
my_manager.DestroyEntitiesWith<HumanComponent, OtherComponent>();
my_manager.DestroyEntitiesWithout<HumanComponent, OtherComponent>();
```
`GetComponentTuple<>()` returns a vector of tuples where the first tuple element is the entity handle and the rest are references to the components in the order of the template parameter list. This is equivalent to the `entities` variable used in systems.
```c++
auto vector_of_tuples = my_manager.GetComponentTuple<HumanComponent, OtherComponent>();
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
