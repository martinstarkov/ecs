#pragma once

#include "common.h"

class EntityBasics {
public:
	EntityBasics() {

		ecs::Entity test;
		ecs::Entity test2;
		ecs::Entity null = ecs::null;

		// Basic and null comparisons
		assert(test == test2);
		assert(test == null);
		assert(null == test);
		assert(test == ecs::null);
		assert(ecs::null == test);

		/*
		// Passes but entity version must be set to const for final version.
		test2.version_ += 1;
		assert(test2 != null);
		assert(null != test2);
		assert(test2 != ecs::null);
		assert(ecs::null != test2);
		*/
		ecs::Manager manager;
		assert(manager.GetEntityCount() == 0);
		auto e0 = manager.CreateEntity();
		assert(manager.GetEntityCount() == 0);
		manager.Refresh();
		assert(manager.GetEntityCount() == 1);
		auto e1 = manager.CreateEntity();
		auto e2 = manager.CreateEntity();
		assert(manager.GetEntityCount() == 1);
		manager.Refresh();
		assert(manager.GetEntityCount() == 3);
		auto e3 = manager.CreateEntity();
		auto e4 = manager.CreateEntity();
		auto e5 = manager.CreateEntity();
		assert(manager.GetEntityCount() == 3);
		manager.Refresh();
		assert(manager.GetEntityCount() == 6);
		e1.Destroy();
		e3.Destroy();
		e5.Destroy();
		assert(manager.GetEntityCount() == 6);
		manager.Refresh();
		assert(manager.GetEntityCount() == 3);
		auto e3_new = manager.CreateEntity();
		assert(manager.GetEntityCount() == 3);
		manager.Refresh();
		assert(manager.GetEntityCount() == 4);
		auto e1_new = manager.CreateEntity();
		auto e5_new = manager.CreateEntity();
		assert(manager.GetEntityCount() == 4);
		manager.Refresh();
		assert(manager.GetEntityCount() == 6);

		// TODO: Fix entity deletion.

		LOG("Entity basics passed");
	}
};