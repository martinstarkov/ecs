#pragma once

#include "ecs/ecs.h"

struct ViewComponent {
	ViewComponent(int hunger) : hunger{ hunger } {}

	int hunger{};
};

bool TestViews() {
	ecs::Manager m;
	std::size_t counter{ 0 };

	m.CreateEntity();
	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(!counter, "Refresh should be triggered for entities to be updated");

	m.CreateEntity();
	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(!counter, "Refresh should be triggered for entities to be updated");

	m.Refresh();
	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(counter == 2, "Refresh failed");

	counter = 0;
	for (auto [e, f] : m.EntitiesWith<ViewComponent>()) {
		counter++;
	}
	ECS_ASSERT(!counter, "EntitiesWith failed");

	for (auto e : m.EntitiesWithout<ViewComponent>()) {
		counter++;
	}
	ECS_ASSERT(counter == 2, "EntitiesWithout failed");

	counter = 0;
	for (auto e : m.Entities()) {
		counter++;
		e.Destroy();
	}
	ECS_ASSERT(counter == 2, "Entity destroy exited early");

	counter = 0;
	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(counter == 2, "Entity destroy should not work until refresh has been called");

	counter = 0;
	m.Refresh();
	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(!counter, "Refresh failed");

	auto e1 = m.CreateEntity();
	auto e2 = m.CreateEntity();

	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(!counter, "Entities should not be added until refresh is called");

	m.Refresh();
	for (auto e : m.Entities()) {
		counter++;
	}
	ECS_ASSERT(counter == 2, "Refresh failed");

	counter = 0;
	for (auto e : m.EntitiesWithout<ViewComponent>()) {
		if (!counter) {
			e1.Add<ViewComponent>(31);
			e2.Add<ViewComponent>(32);
		}
		counter++;
	}
	ECS_ASSERT(
		counter == 1, "Adding components to entities which have not been cycled through yet will "
					  "cause them to fail the criterion check"
	);

	counter = 0;
	ECS_ASSERT(e1.Has<ViewComponent>(), "Component addition inside loop failed");
	ECS_ASSERT(e2.Has<ViewComponent>(), "Component addition inside loop failed");
	ECS_ASSERT(e1.Get<ViewComponent>().hunger == 31, "Component addition inside loop failed");
	ECS_ASSERT(e2.Get<ViewComponent>().hunger == 32, "Component addition inside loop failed");

	for (auto e : m.EntitiesWithout<ViewComponent>()) {
		counter++;
	}
	ECS_ASSERT(!counter, "EntitiesWithout failed after addition of components");

	for (auto [e, f] : m.EntitiesWith<ViewComponent>()) {
		if (counter == 0) {
			e1.Remove<ViewComponent>();
			e2.Remove<ViewComponent>();
		}
		counter++;
	}
	ECS_ASSERT(
		counter == 1,
		"Removing components from entities which have not been cycled through yet will "
		"cause them to fail the criterion check"
	);

	ECS_ASSERT(!e1.Has<ViewComponent>(), "Component removal inside loop failed");
	ECS_ASSERT(!e2.Has<ViewComponent>(), "Component removal inside loop failed");

	return true;
}
