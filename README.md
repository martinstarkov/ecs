# C++ Single-Header Entity Component System Library

## Introduction
This library aims to be as cache-friendly as possible while also supporting runtime addition of new component types. This is achieved by storing all components contiguously in memory in the form of void pointers which are then cast to specific components when required. As far as this approach goes, while it is not type safe, the implementation gets around this by maintaining an indexing table of components with unique type ids for each entity which prevents retrieval / access of invalid component types. 
From a few head-to-head tests for cached looping of components I have managed to achieve a comparative speed of 95% of skypjack's extremely impressive [enTT](https://github.com/skypjack/entt) library, used in Minecraft. I'll catch up eventually ;), after all this is my first library.


## Manager

The core storage unit of entities and systems is the ecs::Manager class, created like so.
```c++
#include "ECS.h"

ecs::Manager my_manager;
```
Note that managers cannot be copied so take special care to always use appropriate move semantics / pointers / references when passing managers to functions / objects.


## Entities

Entities are interacted with through a handle called ecs::Entity, created using the manager.
```c++

ecs::Manager my_manager;

auto entity1 = my_manager.CreateEntity();
```
and destroyed using the handle.
```c++
entity1.Destroy();
```
Important note: Destroying an entity effectively invalidates all handles to that entity by incrementing the internal version counter of that particular entity within the manager. This, however, will not prevent the user from using existing references to entity components (retrieved before destruction of entity) as they remain in memory and are replaced only upon the addition of another component of the same type.

A null entity can be created using ecs::null, this may be useful is attempting to find an entity which matches a specific condition and returning an invalid entity is no such entity is found.
```c++
auto my_invalid_entity = ecs::null;
```
Entity validity is elaborated upon in the end of this subsection.

Cycling through multiple entities without the use of systems can be done via a manager function and a lambda.
```c++
// The lambda parameter should always be a entity handle (ecs::Entity) passed by value as it is created into the parameter list. 
my_manager.ForEachEntity([] (auto entity_handle) {
    // ... Do stuff with entity handles here ...
});
```
Or if specific components are required, by populating the template parameter list of ForEach.
```c++
// The lambda parameters go as follows, first the entity handle (ecs::Entity) passed by value as explained above,
// then all the remaining parameters should be references to the components in the order of the template parameter list. 
my_manager.ForEach<HumanComponent, OtherComponent>([] (auto entity_handle, auto& human_component, auto& other_component) {
    // ... Do stuff with entity handles / components here ...
});
```

Additionally, entity handles contain some properties / functions which may be useful in specific circumstances:
- Overloaded == and != operators for comparison between entities, for this to return true the manager, version, and id of both entities must match.
- Comparison with ecs::null (invalid entity).
- GetId() returns the unique entity id (IMPORTANT NOTE: this should not be used for mapping of any kind as ids are reused upon destruction and creation of new entities)
- GetVerion() returns the version number of the entity id, i.e. how many times the entity id has been used.
- GetManager() returns a pointer to the parent manager, or nullptr if the entity is ecs::null.
- IsAlive() and IsValid() which return bools indicating whether or not the entity is alive (or destroyed) and valid (not null), respectively. 


## Components

Components can be defined without inheriting from a base class. Due to runtime addition support, the manager does not have to be notified of new component additions in compile time.
Every component requires a valid constructor.
```c++
struct HumanComponent {
    HumanComponent(int age, doule height) : age{ age }, height{ height } {}
    int age;
    double height;
}
```

The user can interact with an entity's components through the entity handle.
Adding a component requires you to pass the constructor arguments. If the entity already contains the component, it will be replaced. 
AddComponent also returns a reference to the newly created component.
```c++

auto entity1 = my_manager.CreateEntity();

int age = 22;

double height = 180.5;

auto& human_component = entity1.AddComponent<HumanComponent>(age, height);

human_component.height += 0.5;
```

Component check.
```c++
bool is_human = entity1.HasComponent<HumanComponent>();
```
or for multiple check at once
```c++
bool has_both = entity1.HasComponents<HumanComponent, OtherComponent>();
```

Component retrieval returns a reference to the component.
```c++
auto& human_component = entity1.GetComponent<HumanComponent>();

human_component.age += 1;
```
or for multiple retrieval at once
```c++
auto tuple_of_components = entity1.GetComponents<HumanComponent, OtherComponent>();
```
accessed easily via a structured binding
```c++
auto [human_component, other_component] = tuple_of_components;
```

An important note when using components references is that removing a component while a reference to it exists will not invalidate the reference but will ensure that new ones cannot be retrieved. 
The memory at the reference location is maintained until a new component of the same type is added to an entity (this will override the old component memory).

Component removal.
```c++
entity1.RemoveComponent<HumanComponent>();
```
or for multiple removal at once
```c++
entity1.RemoveComponents<HumanComponent, OtherComponent>();
```

Component replacement asserts that the component exists already, this might be useful if wanting to enforce the component's existence when changing it.
```c++
auto& human_component = entity1.ReplaceComponent<HumanComponent>(23, 176.3);
```


## Systems

Systems cache entities with matching components. When declaring a system, you must inherit from the templated ecs::System<> base class and pass the components which you wish to be necessarily associated with the system.
The system class provides a virtual Update() function which can be overloaded and called later via the manager.
ecs::System contains a cached variable called 'entities' which is a vector of tuples containing an entity handle followed by references to the required components.
'entities' can be looped through using structured bindings and automatically updates when a component is added or removed from any entity.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> {
    void Update() {
        // Names of the structured binding variables can be anything.
        // The order must always start with an entity handle, followed by template parameters in the same order.
        for (auto [entity_handle, human_component, other_component] : entities) {
        
            // ... System logic here ....
            
            // For optional components, a simple HasComponent check followed by GetComponent will suffice.
            if (entity_handle.HasComponent<OptionalComponent>() {
                auto& optional_component = entity_handle.GetComponent<OptionalComponent>();
                
                // ... More logic here ...
            }
        }
    }
}
```
If components are removed from an entity within the loop, the cache will be update accordingly after the Update() function has finished. This means that entities destroyed while looping through 'entities' will still work but give errors upon use as their version is invalidated.

Systems can be registered with the manager as follows.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> { ... };

ecs::Manager my_manager;

my_manager.AddSystem<MySystem>();
```
and updated simply by calling
```c++
my_manager.Update<MySystem>();
```
inside your game loop / wherever.

[WIP] TODO: In the future a RemoveSystem function should be added for a system to become invalid.

Additionally, within the system class one may be interested in accessing the parent manager (perhaps to create additional entities), this can be done via the inherited GetManager() function which returns a reference to the parent manager.
```c++
struct MySystem : public ecs::System<HumanComponent, OtherComponent> {
  void Update() {
      auto& manager = GetManager();
      auto new_entity = manager.CreateEntity();
  }
}
```


## Useful manager utility functions

The manager supports a host of useful functions for retrieving specific entities.
```c++
ecs::Manager my_manager;

// Retrieves a vector of handles to all entities in the manager.
auto vector_of_entities = my_manager.GetEntities();

// Retrieves a vector of handles to all entities in the manager with the given components.
auto vector_of_humans = my_manager.GetEntitiesWith<HumanComponent, OtherComponent>();

// Retrieves a vector of handles to all entities in the manager without the given components.
auto vector_of_aliens = my_manager.GetEntitiesWithout<HumanComponent>();

// Returns the amount of alive entities in the given manager.
auto entity_count = my_manager.GetEntityCount();

// Destroys every single entity in the manager.
my_manager.Clear();
// or equivalently
my_manager.DestroyEntities();

// The following functions behave similarly to the GetEntities(With(out)) functions elaborated upon above.
my_manager.DestroyEntitiesWith<HumanComponent, OtherComponent>();
my_manager.DestroyEntitiesWithout<HumanComponent, OtherComponent>();

// GetComponentTuple returns a vector of tuples where the first tuple element is the entity handle.
// The rest are references to the components in the order of the template parameter list.
// This is equivalent to to how 'entities' is used in Systems.
auto vector_of_tuples = my_manager.GetComponentTuple<HumanComponent, OtherComponent>();
// And can be cycled through easily using a structured binding as before.
for (auto [entity_handle, human_component, other_component] : vectors_of_tuples) {
    // ... Code here ...
}
```
## Thanks to

[WIP] Will update later with all the sources I used for researching ECS. 
