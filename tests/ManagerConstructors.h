#pragma once

#include "common.h"

void ManagerConstructors() {
	ecs::Manager manager;
	ecs::Manager manager2;

	assert(manager.Equivalent(manager));
	assert(manager.Equivalent(manager2));
	assert(manager != manager2);

	auto manager3 = std::move(manager);

	assert(!manager.Equivalent(manager3));
	assert(!manager.Equivalent(manager2));
	assert(manager != manager3);
	LOG("ManagerConstructors passed");
}