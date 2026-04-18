#pragma once

#include "ecs/ecs.h"

struct ViewComponent {
	ViewComponent(int hunger) : hunger{ hunger } {}

	int hunger{};
};

bool TestViewUtility() {
	ecs::Manager m;

	auto e1 = m.CreateEntity();
	auto e2 = m.CreateEntity();

	m.Refresh();

	// Re-add components for view utility tests.
	e1.Add<ViewComponent>(31);
	e2.Add<ViewComponent>(32);

	auto with_view = m.EntitiesWith<ViewComponent>();
	auto all_view  = m.Entities();

	// Count
	ECS_ASSERT(with_view.Count() == 2, "View Count failed");
	ECS_ASSERT(all_view.Count() == 2, "Entity Count failed");

	// Contains
	ECS_ASSERT(with_view.Contains(e1), "View Contains failed");
	ECS_ASSERT(with_view.Contains(e2), "View Contains failed");
	ECS_ASSERT(!with_view.Contains({}), "View Contains should fail for null entity");

	// AnyOf
	ECS_ASSERT(
		with_view.AnyOf([](const auto& e, const auto& c) { return c.hunger == 31; }),
		"View AnyOf failed for entity + component predicate"
	);
	ECS_ASSERT(
		all_view.AnyOf([&](const auto& e) { return e == e1; }),
		"View AnyOf failed for entity-only predicate"
	);
	ECS_ASSERT(
		!with_view.AnyOf([](const auto& e, const auto& c) { return c.hunger == 99; }),
		"View AnyOf returned true incorrectly"
	);

	// AllOf
	ECS_ASSERT(
		with_view.AllOf([](const auto& e, const auto& c) { return c.hunger > 30; }),
		"View AllOf failed"
	);
	ECS_ASSERT(
		!with_view.AllOf([](const auto& e, const auto& c) { return c.hunger == 31; }),
		"View AllOf returned true incorrectly"
	);

	// CountIf
	ECS_ASSERT(
		with_view.CountIf([](const auto& e, const auto& c) { return c.hunger >= 32; }) == 1,
		"View CountIf failed"
	);
	ECS_ASSERT(
		all_view.CountIf([&](const auto& e) { return e == e1 || e == e2; }) == 2,
		"Entity view CountIf failed"
	);

	// FindIf
	auto found1 = with_view.FindIf([](const auto& e, const auto& c) { return c.hunger == 31; });
	ECS_ASSERT(found1 == e1, "View FindIf failed to find correct entity");

	auto found2 = all_view.FindIf([&](const auto& e) { return e == e2; });
	ECS_ASSERT(found2 == e2, "Entity view FindIf failed");

	auto not_found = with_view.FindIf([](const auto& e, const auto& c) { return c.hunger == 999; });
	ECS_ASSERT(!not_found, "View FindIf should return null entity when not found");

	// ForEach
	std::size_t foreach_count{ 0 };
	int hunger_sum{ 0 };
	with_view.ForEach([&](const auto& e, const auto& c) {
		++foreach_count;
		hunger_sum += c.hunger;
	});
	ECS_ASSERT(foreach_count == 2, "View ForEach failed to iterate");
	ECS_ASSERT(hunger_sum == 63, "View ForEach failed to pass components");

	std::size_t entity_foreach_count{ 0 };
	all_view.ForEach([&](const auto& e) { ++entity_foreach_count; });
	ECS_ASSERT(entity_foreach_count == 2, "Entity view ForEach failed");

	// Transform
	auto hungers = with_view.Transform([](const auto& e, const auto& c) { return c.hunger; });
	ECS_ASSERT(hungers.size() == 2, "View Transform size failed");
	ECS_ASSERT(
		(hungers[0] == 31 && hungers[1] == 32) || (hungers[0] == 32 && hungers[1] == 31),
		"View Transform values failed"
	);

	auto ids = all_view.Transform([](const auto& e) { return e.GetId(); });
	ECS_ASSERT(ids.size() == 2, "Entity view Transform failed");

	// GetVector
	auto vec = with_view.GetVector();
	ECS_ASSERT(vec.size() == 2, "View GetVector failed");
	ECS_ASSERT(
		(vec[0] == e1 || vec[1] == e1) && (vec[0] == e2 || vec[1] == e2),
		"View GetVector returned wrong entities"
	);

	return true;
}

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

	bool success{ TestViewUtility() };

	return success;
}
