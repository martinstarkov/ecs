#pragma once

#include "common.h"

struct Test {
	Test() = delete;
	Test(int a) : a{a} {}
	int a;
	int b = 0;
	int c = 1;
	int d = 2;
};

struct Test2 {
	Test2() = delete;
	Test2(int z) : z{ z } {}
	int z;
	int x = 0;
	int y = 1;
	int w = 2;
};

void static SpeedTests() {	
	Timer creation;
	Timer refresh;
	Timer entity_retrieval;
	Timer addition;
	Timer has;
	Timer get_retrieval;
	Timer removal;
	Timer destruction;

	ecs::Manager manager;

	//int e = 100000000; // 100 million
	int e = 1000;

	creation.Start();
	for (auto i = 0; i < e; ++i) {
		manager.CreateEntity();
	}
	LOG("Entity creation (" << e << ") took " << creation.ElapsedSeconds() << "s");
	refresh.Start();
	manager.Refresh();
	LOG("Entity refresh took (" << e << ") took " << refresh.ElapsedSeconds() << "s");

	refresh.Start();
	manager.Refresh();
	LOG("Useless Entity refresh took (" << e << ") took " << refresh.ElapsedSeconds() << "s");

	entity_retrieval.Start();
	auto entities = manager.GetEntities();
	assert(entities.size() == e);
	LOG("Group entity retrieval (" << e << ") took " << entity_retrieval.ElapsedSeconds() << "s");
		
	addition.Start();
	for (int i = 0; static_cast<std::size_t>(i) < entities.size(); ++i) {
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
	for (std::size_t i = 0; i < entities.size(); ++i) {
		auto& entity = entities[i];
		auto& comp = entity.GetComponent<Test>();
		assert(comp.a == i);
		auto& comp2 = entity.GetComponent<Test2>();
		assert(comp2.z == i);
		comp.d += 1;
		comp2.w += 3;
	}
	LOG("Component retrieval (" << e << ") took " << get_retrieval.ElapsedSeconds() << "s");

	removal.Start();
	for (auto& entity : entities) {
		entity.RemoveComponent<Test>();
		entity.RemoveComponent<Test2>();
	}
	LOG("Component removal (" << e << ") took " << removal.ElapsedSeconds() << "s");

	//for (auto& entity : entities) entity.AddComponent<Test>(99);

	destruction.Start();
	for (auto& entity : entities) {
		entity.Destroy();
	}
	LOG("Entity destruction (" << e << ") took " << destruction.ElapsedSeconds() << "s");
	refresh.Start();
	manager.Refresh();
	LOG("Entity refresh (" << e << ") took " << refresh.ElapsedSeconds() << "s");
		
	LOG("Manager basics passed");
}