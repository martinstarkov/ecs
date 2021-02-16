#pragma once

#include "Timer.h"
#include "../ECS_old.h"

#include <iostream>
#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

struct Test {
	Test(int a) : a{a} {}
	int a;
};

class SpeedTests {
public:
	SpeedTests() {
		Timer creation;
		Timer entity_retrieval;
		Timer addition;
		Timer has;
		Timer get_retrieval;
		Timer removal;
		Timer destruction;

		ecs::Manager manager;

		int e = 100000000;

		creation.Start();
		for (auto i = 0; i < e; ++i) {
			manager.CreateEntity();
		}
		//manager.Refresh();
		LOG("Entity creation (" << e << ") took " << creation.ElapsedSeconds() << "s");

		entity_retrieval.Start();
		auto entities = manager.GetEntities();
		assert(entities.size() == e);
		LOG("Group entity retrieval (" << e << ") took " << entity_retrieval.ElapsedSeconds() << "s");

		addition.Start();
		for (auto& entity : entities) {
			entity.AddComponent<Test>(5);
		}
		LOG("Component addition (" << e << ") took " << addition.ElapsedSeconds() << "s");

		has.Start();
		for (auto& entity : entities) {
			if (!entity.HasComponent<Test>()) {
				int i = 0;
				i += 1;
			}
		}
		LOG("Component has check (" << e << ") took " << has.ElapsedSeconds() << "s");

		get_retrieval.Start();
		for (auto& entity : entities) {
			auto& comp = entity.GetComponent<Test>();
			comp.a += 1;
		}
		LOG("Component retrieval (" << e << ") took " << get_retrieval.ElapsedSeconds() << "s");

		removal.Start();
		for (auto& entity : entities) {
			if (entity.GetId() == 5000 || entity.GetId() == 5001 || entity.GetId() == 5002) {
				bool test = true;
			}
			entity.RemoveComponent<Test>();
		}
		LOG("Component removal (" << e << ") took " << removal.ElapsedSeconds() << "s");

		/*for (auto& entity : entities) {
			entity.AddComponent<Test>(99);
		}*/

		destruction.Start();
		for (auto& entity : entities) {
			entity.Destroy();
		}
		//manager.Refresh();
		LOG("Entity destruction (" << e << ") took " << destruction.ElapsedSeconds() << "s");
	
		LOG("Manager basics passed");

	}
};