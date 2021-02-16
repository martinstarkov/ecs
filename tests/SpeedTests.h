#pragma once

#include "Timer.h"
#include "../ECS.h"

#include <iostream>
#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

struct Test {
	Test(int a) : a{a} {}
	int a;
	int b = 0;
	int c = 1;
	int d = 2;
};

struct Test2 {
	Test2(int z) : z{ z } {}
	int z;
	int x = 0;
	int y = 1;
	int w = 2;
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

		int e = 10;

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
		for (std::size_t i = 0; i < entities.size(); ++i) {
			auto& entity = entities[i];
			entity.AddComponent<Test>(i);
			entity.AddComponent<Test2>(i);
		}
		LOG("Component addition (" << e << ") took " << addition.ElapsedSeconds() << "s");

		has.Start();
		for (auto& entity : entities) {
			if (!entity.HasComponent<Test>() && !entity.HasComponent<Test2>()) {
				int i = 0;
				i += 1;
			}
		}
		LOG("Component has check (" << e << ") took " << has.ElapsedSeconds() << "s");

		get_retrieval.Start();
		for (auto& entity : entities) {
			auto& comp = entity.GetComponent<Test>();
			auto& comp2 = entity.GetComponent<Test2>();
			comp.d += 1;
			comp2.w += 3;
		}
		LOG("Component retrieval (" << e << ") took " << get_retrieval.ElapsedSeconds() << "s");

		removal.Start();
		for (auto& entity : entities) {
			if (entity.GetId() == 4) {
				bool test = true;
			}
			entity.RemoveComponent<Test>();
			entity.RemoveComponent<Test2>();
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