#pragma once

#include "common.h"

static void ManagerBasics() {
	ecs::Manager manager;
	ecs::Manager manager2;

	// Empty equality test.
	assert(manager == manager2);
	assert(manager2 == manager);

	// Move construction.
	ecs::Manager manager3 = std::move(manager2);
	assert(manager == manager3);
	assert(manager == manager2);
	auto manager4 = std::move(manager3.Clone());
	assert(manager4 == manager3);

	auto manager5 = manager4.Clone();
	assert(manager5 == manager4);

	LOG("Manager basics passed");

}