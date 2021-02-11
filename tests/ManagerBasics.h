#pragma once

#include "common.h"

class ManagerBasics {
public:
	ManagerBasics() {
		ecs::Manager manager;
		ecs::Manager manager2;

		// Empty equality test.
		assert(manager == manager2);
		assert(manager2 == manager);

		// auto new_manager = manager; // does not compile as manager copying is not allowed.

		// Entity difference equality test.
		manager2.entities_.resize(2);
		assert(manager != manager2);
		assert(manager2 != manager);
		manager2.entities_.pop_back();
		assert(manager == manager2);

		// Pool difference equality test.
		manager2.component_pools_.resize(1);
		assert(manager != manager2);
		manager2.component_pools_.pop_back();
		assert(manager == manager2);

		// Move construction.
		manager.component_pools_.resize(1);
		manager2.component_pools_.resize(1);
		auto manager3 = std::move(manager2);
		assert(manager == manager3);
		assert(manager != manager2);

		LOG("Manager basics passed");

	}
};