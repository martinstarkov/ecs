#pragma once

#include <iostream>
#include <utility>

#include "common.h"
#include "ecs/ecs.h"
#include "hooks.h"
#include "views.h"

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
	std::cout << "Starting ECS tests..." << std::endl;

	ecs::Manager manager;

	// Entity creation
	ecs::Entity e1	 = manager.CreateEntity();
	ecs::Entity from = manager.CreateEntity();
	manager.Refresh();
	ecs::Entity to = manager.CreateEntity();

	// Entity identity tests
	manager.Refresh();
	ecs::Entity e2 = e1;
	ECS_ASSERT(e2.IsIdenticalTo(e1), "");

	ECS_ASSERT(e1.IsAlive(), "");
	ECS_ASSERT(e2.IsAlive(), "");

	e1.Destroy();
	ECS_ASSERT(e1.IsAlive(), "");
	ECS_ASSERT(e2.IsAlive(), "");

	manager.Refresh();
	ECS_ASSERT(!e1.IsAlive(), "");
	ECS_ASSERT(!e2.IsAlive(), "");

	ECS_ASSERT(from.IsAlive(), "");
	ECS_ASSERT(to.IsAlive(), "");

	ecs::Entity entity = manager.CreateEntity();
	ECS_ASSERT(manager == entity.GetManager(), "");

	// Component add / get / remove
	int age		  = 22;
	double height = 180.5;

	HumanComponent& human  = entity.Add<HumanComponent>(age, height);
	human.height		  += 0.5;

	bool is_human = entity.Has<HumanComponent>();
	ECS_ASSERT(is_human, "");

	bool is_cyborg = entity.Has<HumanComponent, RobotComponent>();
	ECS_ASSERT(!is_cyborg, "");

	bool is_human_or_cyborg = entity.HasAny<HumanComponent, RobotComponent>();
	ECS_ASSERT(is_human_or_cyborg, "");

	bool is_cyborg_or_human = entity.HasAny<RobotComponent, HumanComponent>();
	ECS_ASSERT(is_cyborg_or_human, "");

	entity.TryAdd<RobotComponent>(34);
	entity.Add<RobotComponent>(33); // replaces previous component

	is_cyborg = entity.Has<HumanComponent, RobotComponent>();
	ECS_ASSERT(is_cyborg, "");

	HumanComponent& human5	= entity.Get<HumanComponent>();
	human5.age			   += 1;
	ECS_ASSERT(human5.age == 23, "");

	ECS_ASSERT((entity.Has<RobotComponent, HumanComponent>()), "");

	auto [robot, h] = entity.Get<RobotComponent, HumanComponent>();
	ECS_ASSERT(robot.id == 33, "");

	entity.TryAdd<RobotComponent>(34); // does not replace
	auto robot_ref2 = entity.Get<RobotComponent>();
	ECS_ASSERT(robot_ref2.id == 33, "");
	ECS_ASSERT(h.age == 23, "");

	auto human_pointer = entity.TryGet<HumanComponent>();
	ECS_ASSERT(human_pointer != nullptr, "");

	entity.Remove<HumanComponent>();
	ECS_ASSERT(!entity.Has<HumanComponent>(), "");

	auto inhuman_pointer = entity.TryGet<HumanComponent>();
	ECS_ASSERT(inhuman_pointer == nullptr, "");

	ECS_ASSERT(!entity.Has<AlienComponent>(), "");

	entity.Remove<RobotComponent, AlienComponent>();
	ECS_ASSERT(!entity.Has<RobotComponent>(), "");
	ECS_ASSERT(!entity.Has<AlienComponent>(), "");
	ECS_ASSERT(!(entity.Has<RobotComponent, AlienComponent>()), "");

	// Create more entities
	auto entity2 = manager.CreateEntity();
	auto entity3 = manager.CreateEntity();
	auto entity4 = manager.CreateEntity();

	manager.Refresh();

	// Add components to all entities
	for (auto e : manager.Entities()) {
		e.Add<ZombieComponent>(1);
		e.Add<FoodComponent>(1);
	}

	ECS_ASSERT(entity.Has<FoodComponent>(), "");
	ECS_ASSERT(entity.Has<ZombieComponent>(), "");
	ECS_ASSERT(entity2.Has<FoodComponent>(), "");
	ECS_ASSERT(entity2.Has<ZombieComponent>(), "");
	ECS_ASSERT(entity3.Has<FoodComponent>(), "");
	ECS_ASSERT(entity3.Has<ZombieComponent>(), "");
	ECS_ASSERT(entity4.Has<FoodComponent>(), "");
	ECS_ASSERT(entity4.Has<ZombieComponent>(), "");

	entity.Get<FoodComponent>().hunger	 = 101;
	entity.Get<ZombieComponent>().number = 99;
	entity2.Get<FoodComponent>().hunger	 = 102;

	int threshold = 100;

	// Const view iteration
	for (auto [e, zombie, food] :
		 std::as_const(manager).EntitiesWith<ZombieComponent, FoodComponent>()) {
		ECS_ASSERT(e.Has<ZombieComponent>(), "");
		ECS_ASSERT(e.Has<FoodComponent>(), "");
	}

	// Mutating view iteration
	for (auto [e, zombie, food] : manager.EntitiesWith<ZombieComponent, FoodComponent>()) {
		if (food.hunger < threshold) {
			e.Destroy();
		}
	}

	ECS_ASSERT(entity.IsAlive(), "");
	ECS_ASSERT(entity2.IsAlive(), "");
	ECS_ASSERT(entity3.IsAlive(), "");
	ECS_ASSERT(entity4.IsAlive(), "");

	manager.Refresh();

	ECS_ASSERT(entity.IsAlive(), "");
	ECS_ASSERT(entity2.IsAlive(), "");
	ECS_ASSERT(!entity3.IsAlive(), "");
	ECS_ASSERT(!entity4.IsAlive(), "");

	entity2.Remove<FoodComponent>();

	// EntitiesWithout
	for (auto e : manager.EntitiesWithout<FoodComponent>()) {
		ECS_ASSERT((!e.Has<FoodComponent>()), "");
		e.Destroy();
	}

	ECS_ASSERT(entity.IsAlive(), "");
	ECS_ASSERT(entity2.IsAlive(), "");

	manager.Refresh();
	ECS_ASSERT(entity.IsAlive(), "");
	ECS_ASSERT(!entity2.IsAlive(), "");

	// Copy entity
	auto new_entity		  = manager.CopyEntity(entity);
	auto new_entity_other = entity.Copy();

	ECS_ASSERT(new_entity.IsIdenticalTo(entity), "");
	ECS_ASSERT(entity.IsIdenticalTo(new_entity), "");
	ECS_ASSERT(new_entity_other.IsIdenticalTo(entity), "");
	ECS_ASSERT(entity.IsIdenticalTo(new_entity_other), "");

	ECS_ASSERT((entity.Has<FoodComponent, ZombieComponent>()), "");
	ECS_ASSERT((new_entity.Has<FoodComponent, ZombieComponent>()), "");

	auto [f1, z1] = entity.Get<FoodComponent, ZombieComponent>();
	auto [f2, z2] = new_entity.Get<FoodComponent, ZombieComponent>();

	ECS_ASSERT(f1.hunger == f2.hunger, "");
	ECS_ASSERT(z1.number == 99, "");
	ECS_ASSERT(z1.number == z2.number, "");

	// Copy only one component
	ecs::Entity new_entity2 = manager.CopyEntity<ZombieComponent>(entity);

	ECS_ASSERT(!new_entity2.IsIdenticalTo(entity), "");
	ECS_ASSERT((entity.Has<FoodComponent, ZombieComponent>()), "");
	ECS_ASSERT(new_entity2.Has<ZombieComponent>(), "");
	ECS_ASSERT(!new_entity2.Has<FoodComponent>(), "");

	auto& z4 = entity.Get<ZombieComponent>();
	auto& z5 = new_entity2.Get<ZombieComponent>();
	auto& z6 = new_entity2.Get<ZombieComponent>();

	ECS_ASSERT(z4.number == 99, "");
	ECS_ASSERT(z4.number == z5.number, "");
	ECS_ASSERT(z5.number == z6.number, "");

	manager.Refresh();

	ECS_ASSERT(manager.Size() == 4, ""); // entity, new_entity, new_entity2, new_entity_other

	new_entity.Destroy();
	new_entity_other.Destroy();

	ECS_ASSERT(manager.Size() == 4, "");
	manager.Refresh();
	ECS_ASSERT(manager.Size() == 2, "");

	auto new_entity3 = manager.CreateEntity();
	ECS_ASSERT(manager.Size() == 2, "");
	manager.Refresh();
	ECS_ASSERT(manager.Size() == 3, "");

	// Manager reset + copy
	manager.Clear();
	ECS_ASSERT(manager.Size() == 0, "");

	manager.Reset();
	manager.Reserve(5);

	manager.CreateEntity();
	manager.CreateEntity();
	auto test_e = manager.CreateEntity();
	test_e.Add<ZombieComponent>(3);

	ECS_ASSERT(manager.Size() == 0, "");
	manager.Refresh();
	ECS_ASSERT(manager.Size() == 3, "");

	auto new_manager = manager;
	ECS_ASSERT(new_manager != manager, "");
	ECS_ASSERT(new_manager.Size() == 3, "");
	ECS_ASSERT(manager.Size() == 3, "");

	Print("ECS basic tests passed!");

	TestHooks();
	Print("ECS hook tests passed!");

	TestViews();
	Print("ECS view tests passed!");

	Print("ECS tests passed!");

	return true;
}
