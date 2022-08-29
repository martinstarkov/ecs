#pragma once

#include <cassert>  // assert
#include <iostream> // std::cout

#include "ecs/ecs.h"

struct HumanComponent {
	HumanComponent(int age, double height) : age{ age }, height{ height } {}
	int age{};
	double height{};
};

struct RobotComponent {
	RobotComponent(int id) : id{ id } {}
	int id{};
};

struct AlienComponent {
	AlienComponent(int planet) : planet{ planet } {}
	int planet{};
};

struct ZombieComponent {
	ZombieComponent(int number) : number{ number } {}
	int number{};
};

struct FoodComponent {
	FoodComponent(int hunger) : hunger{ hunger } {}
	int hunger{};
};

bool TestECS() {
	std::cout << "Starting tests..." << std::endl;

	ecs::Manager manager;
	ecs::Entity e1 = manager.CreateEntity();
	manager.Refresh();
	ecs::Entity e2 = e1;
	assert(e2.IsIdenticalTo(e1));
	assert(e1.IsAlive());
	assert(e2.IsAlive());
	e1.Destroy();
	assert(e1.IsAlive());
	assert(e2.IsAlive());
	manager.Refresh();
	assert(!e1.IsAlive());
	assert(!e2.IsAlive());
	auto permanently_invalid_entity = ecs::null;
	assert(e2 != permanently_invalid_entity);
	e2 = ecs::null;
	assert(e2 == permanently_invalid_entity);
	assert(permanently_invalid_entity == e2);
	ecs::Entity entity = manager.CreateEntity();
	// Note that the Refresh() comes much later.
	assert(manager == entity.GetManager());

	int age = 22;
	double height = 180.5;

	auto& human = entity.AddComponent<HumanComponent>(age, height);
	human.height += 0.5;

	bool is_human = entity.HasComponent<HumanComponent>();
	assert(is_human);
	bool is_cyborg = entity.HasComponents<HumanComponent, RobotComponent>();
	assert(!is_cyborg);
	entity.AddComponent<RobotComponent>(33);
	is_cyborg = entity.HasComponents<HumanComponent, RobotComponent>();
	assert(is_cyborg);

	auto& human5 = entity.GetComponent<HumanComponent>();
	human5.age += 1;
	assert(human5.age == 22 + 1);

	assert((entity.HasComponents<RobotComponent, HumanComponent>()));
	auto [robot, h] = entity.GetComponents<RobotComponent, HumanComponent>();
	assert(robot.id == 33);
	assert(h.age == 22 + 1);
	entity.RemoveComponent<HumanComponent>();
	assert(!entity.HasComponent<HumanComponent>());

	assert(!entity.HasComponent<AlienComponent>());
	entity.RemoveComponents<RobotComponent, AlienComponent>();
	assert(!entity.HasComponent<RobotComponent>());
	assert(!entity.HasComponent<AlienComponent>());
	assert(!(entity.HasComponents<RobotComponent, AlienComponent>()));

	auto entity2 = manager.CreateEntity();
	auto entity3 = manager.CreateEntity();
	auto entity4 = manager.CreateEntity();

	manager.Refresh();

	manager.ForEachEntity([](ecs::Entity entity) {
		entity.AddComponent<ZombieComponent>(1);
		entity.AddComponent<FoodComponent>(1);
	});

	entity.GetComponent<FoodComponent>().hunger = 101;
	entity.GetComponent<ZombieComponent>().number = 99;
	entity2.GetComponent<FoodComponent>().hunger = 101;

	int threshold = 100;

	manager.ForEachEntityWith<ZombieComponent, FoodComponent>(
		[&](ecs::Entity e, auto& zombie, auto& food) {
		if (food.hunger < threshold) {
			e.Destroy();
		}
	});
	assert(entity.IsAlive());
	assert(entity2.IsAlive());
	assert(entity3.IsAlive());
	assert(entity4.IsAlive());
	manager.Refresh();
	assert(entity.IsAlive());
	assert(entity2.IsAlive());
	assert(!entity3.IsAlive());
	assert(!entity4.IsAlive());

	entity2.RemoveComponent<FoodComponent>();

	manager.ForEachEntityWithout<FoodComponent>([&](ecs::Entity e) {
		e.Destroy();
	});
	assert(entity.IsAlive());
	assert(entity2.IsAlive());
	manager.Refresh();
	assert(entity.IsAlive());
	assert(!entity2.IsAlive());

	auto new_entity = manager.CopyEntity(entity);
	// Note the late manager.Refresh() call.

	assert(new_entity.IsIdenticalTo(entity));
	assert((entity.HasComponents<FoodComponent, ZombieComponent>()));
	assert((new_entity.HasComponents<FoodComponent, ZombieComponent>()));

	auto [f1, z1] = entity.GetComponents<FoodComponent, ZombieComponent>();
	auto [f2, z2] = new_entity.GetComponents<FoodComponent, ZombieComponent>();
	assert(f1.hunger == f2.hunger);
	assert(z1.number == 99);
	assert(z1.number == z2.number);

	ecs::Entity new_entity2 = manager.CopyEntity<ZombieComponent>(entity);
	// Note the late manager.Refresh() call.
	assert(!new_entity2.IsIdenticalTo(entity));
	assert((entity.HasComponents<FoodComponent, ZombieComponent>()));
	assert(new_entity2.HasComponent<ZombieComponent>());
	assert(!new_entity2.HasComponent<FoodComponent>());
	auto [z4] = entity.GetComponents<ZombieComponent>();
	auto [z5] = new_entity2.GetComponents<ZombieComponent>();
	auto& z6 = new_entity2.GetComponent<ZombieComponent>();
	assert(z4.number == 99);
	assert(z4.number == z5.number);
	assert(z5.number == z6.number);

	manager.Refresh();

	assert(manager.GetEntityCount() == 3); // entity, new_entity, new_entity2.
	new_entity.Destroy();
	assert(manager.GetEntityCount() == 3); // entity, new_entity, new_entity2.
	manager.Refresh();
	assert(manager.GetEntityCount() == 2); // entity, new_entity2.
	auto new_entity3 = manager.CreateEntity();
	assert(manager.GetEntityCount() == 2); // entity, new_entity2, new_entity3.
	manager.Refresh();
	assert(manager.GetEntityCount() == 3); // entity, new_entity2, new_entity3.

	manager.Clear();
	assert(manager.GetEntityCount() == 0);
	manager.Reset();
	manager.Reserve(5);
	manager.CreateEntity();
	manager.CreateEntity();
	manager.CreateEntity();
	assert(manager.GetEntityCount() == 0);
	manager.Refresh();
	assert(manager.GetEntityCount() == 3);
	auto new_manager = manager.Clone();
	assert(new_manager == manager);
	assert(manager.GetEntityCount() == 3);
	assert(new_manager.GetEntityCount() == 3);

	std::cout << "All tests passed!" << std::endl;
	return true;
}