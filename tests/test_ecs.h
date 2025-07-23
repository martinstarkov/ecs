#pragma once

#include <cassert>
#include <chrono>
#include <iostream>
#include <utility>

#include "ecs/ecs.h"

struct HookComponent {
	HookComponent() = default;

	int irrelevant{ 0 };
};

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

	/*ZombieComponent(ZombieComponent&&) = default;
	ZombieComponent& operator=(ZombieComponent&&) = default;
	ZombieComponent(const ZombieComponent&) = delete;
	ZombieComponent& operator=(const ZombieComponent&) = delete;*/

	int number{};
};

struct FoodComponent {
	FoodComponent(int hunger) : hunger{ hunger } {}

	int hunger{};
};

void ConstructHook1(ecs::Entity e) {
	std::cout << "Hook: Constructed HookComponent for ";
	std::cout << e.GetId() << std::endl;
}

void ConstructHook2(ecs::Entity e) {
	std::cout << "Another hook: Constructed HookComponent for ";
	std::cout << e.GetId() << std::endl;
}

void UpdateHook1(ecs::Entity e) {
	std::cout << "Hook: Updated HookComponent for ";
	std::cout << e.GetId() << std::endl;
}

void DestructHook1(ecs::Entity e) {
	std::cout << "Hook: Destructed HookComponent for ";
	std::cout << e.GetId() << std::endl;
}

bool TestHooks() {
	ecs::Manager manager;

	auto construct_hook1 = manager.OnConstruct<HookComponent>().Connect<&ConstructHook1>();
	assert(manager.HasOnConstruct<HookComponent>(construct_hook1));
	auto construct_hook2 = manager.OnConstruct<HookComponent>().Connect<&ConstructHook2>();
	assert(manager.HasOnConstruct<HookComponent>(construct_hook2));
	auto update_hook1 = manager.OnUpdate<HookComponent>().Connect<&UpdateHook1>();
	assert(manager.HasOnUpdate<HookComponent>(update_hook1));
	auto destruct_hook1 = manager.OnDestruct<HookComponent>().Connect<&DestructHook1>();
	assert(manager.HasOnDestruct<HookComponent>(destruct_hook1));

	ecs::Entity e0 = manager.CreateEntity();
	ecs::Entity e1 = manager.CreateEntity();
	ecs::Entity e2 = manager.CreateEntity();
	ecs::Entity e3 = manager.CreateEntity();
	ecs::Entity e4 = manager.CreateEntity();

	e0.Add<HookComponent>();
	e1.Add<HookComponent>();

	manager.RemoveOnConstruct<HookComponent>(construct_hook2);
	assert(!manager.HasOnConstruct<HookComponent>(construct_hook2));

	e2.Add<HookComponent>();
	e3.Add<HookComponent>();
	e4.Add<HookComponent>();

	e0.Update<HookComponent>();
	e1.Update<HookComponent>();

	manager.RemoveOnUpdate<HookComponent>(update_hook1);
	assert(!manager.HasOnUpdate<HookComponent>(update_hook1));

	e2.Update<HookComponent>();
	e3.Update<HookComponent>();
	e4.Update<HookComponent>();

	e0.Remove<HookComponent>();
	e1.Remove<HookComponent>();

	e2.Clear();
	e3.Destroy();

	manager.Refresh();

	manager.RemoveOnDestruct<HookComponent>(destruct_hook1);
	assert(!manager.HasOnDestruct<HookComponent>(destruct_hook1));

	e4.Remove<HookComponent>();

	std::cout << "ECS hook tests passed!" << std::endl;

	return true;
}

bool TestECS() {
	std::cout << "Starting ECS tests..." << std::endl;

	ecs::Manager manager;
	ecs::Entity e1	 = manager.CreateEntity();
	ecs::Entity from = manager.CreateEntity();
	manager.Refresh();
	ecs::Entity to = manager.CreateEntity();

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
	assert(from.IsAlive());
	assert(to.IsAlive());
	ecs::Entity entity = manager.CreateEntity();
	assert(manager == entity.GetManager());

	int age		  = 22;
	double height = 180.5;

	HumanComponent& human  = entity.Add<HumanComponent>(age, height);
	human.height		  += 0.5;

	bool is_human = entity.Has<HumanComponent>();
	assert(is_human);
	bool is_cyborg = entity.Has<HumanComponent, RobotComponent>();
	assert(!is_cyborg);
	bool is_human_or_cyborg = entity.HasAny<HumanComponent, RobotComponent>();
	assert(is_human_or_cyborg);
	bool is_cyborg_or_human = entity.HasAny<RobotComponent, HumanComponent>();
	assert(is_cyborg_or_human);
	entity.TryAdd<RobotComponent>(34);
	entity.Add<RobotComponent>(33); // replaces previous component
	is_cyborg = entity.Has<HumanComponent, RobotComponent>();
	assert(is_cyborg);

	HumanComponent& human5	= entity.Get<HumanComponent>();
	human5.age			   += 1;
	assert(human5.age == 22 + 1);

	assert((entity.Has<RobotComponent, HumanComponent>()));
	auto [robot, h] = entity.Get<RobotComponent, HumanComponent>();
	assert(robot.id == 33);
	entity.TryAdd<RobotComponent>(34); // does not replace previous component.
	auto robot_ref2{ entity.Get<RobotComponent>() };
	assert(robot_ref2.id == 33);
	assert(h.age == 22 + 1);
	auto human_pointer{ entity.TryGet<HumanComponent>() };
	assert(human_pointer != nullptr);
	entity.Remove<HumanComponent>();
	assert(!entity.Has<HumanComponent>());
	auto inhuman_pointer{ entity.TryGet<HumanComponent>() };
	assert(inhuman_pointer == nullptr);

	assert(!entity.Has<AlienComponent>());
	entity.Remove<RobotComponent, AlienComponent>();
	assert(!entity.Has<RobotComponent>());
	assert(!entity.Has<AlienComponent>());
	assert(!(entity.Has<RobotComponent, AlienComponent>()));

	auto entity2 = manager.CreateEntity();
	auto entity3 = manager.CreateEntity();
	auto entity4 = manager.CreateEntity();

	manager.Refresh();

	for (auto e : manager.Entities()) {
		e.Add<ZombieComponent>(1);
		e.Add<FoodComponent>(1);
	}

	assert(entity.Has<FoodComponent>());
	assert(entity.Has<ZombieComponent>());
	assert(entity2.Has<FoodComponent>());
	assert(entity2.Has<ZombieComponent>());
	assert(entity3.Has<FoodComponent>());
	assert(entity3.Has<ZombieComponent>());
	assert(entity4.Has<FoodComponent>());
	assert(entity4.Has<ZombieComponent>());

	entity.Get<FoodComponent>().hunger	 = 101;
	entity.Get<ZombieComponent>().number = 99;
	entity2.Get<FoodComponent>().hunger	 = 102;

	int threshold = 100;

	for (auto [e, zombie, food] :
		 std::as_const(manager).EntitiesWith<ZombieComponent, FoodComponent>()) {
		assert(e.Has<ZombieComponent>());
		assert(e.Has<FoodComponent>());
		// zombie.number += 1; // Fails to compile due to const manager.
	}

	for (auto [e, zombie, food] : manager.EntitiesWith<ZombieComponent, FoodComponent>()) {
		if (food.hunger < threshold) {
			e.Destroy();
		}
	}

	/*auto s111 = manager.EntitiesWith<ZombieComponent, FoodComponent>();
	for (auto [e, zombie, food] : s111) {
	  if (food.hunger < threshold) {
		e.Destroy();
	  }
	}*/

	assert(entity.IsAlive());
	assert(entity2.IsAlive());
	assert(entity3.IsAlive());
	assert(entity4.IsAlive());
	manager.Refresh();
	assert(entity.IsAlive());
	assert(entity2.IsAlive());
	assert(!entity3.IsAlive());
	assert(!entity4.IsAlive());

	entity2.Remove<FoodComponent>();

	for (auto e : manager.EntitiesWithout<FoodComponent>()) {
		assert((!e.Has<FoodComponent>()));
		e.Destroy();
	}

	assert(entity.IsAlive());
	assert(entity2.IsAlive());
	manager.Refresh();
	assert(entity.IsAlive());
	assert(!entity2.IsAlive());

	auto new_entity = manager.CopyEntity(entity);
	// Alternative way of copying entity.
	auto new_entity_other = entity.Copy();
	// Note the late manager.Refresh() call.

	assert(new_entity.IsIdenticalTo(entity));
	assert(entity.IsIdenticalTo(new_entity));
	assert(new_entity_other.IsIdenticalTo(entity));
	assert(entity.IsIdenticalTo(new_entity_other));

	assert((entity.Has<FoodComponent, ZombieComponent>()));
	assert((new_entity.Has<FoodComponent, ZombieComponent>()));

	auto [f1, z1] = entity.Get<FoodComponent, ZombieComponent>();
	auto [f2, z2] = new_entity.Get<FoodComponent, ZombieComponent>();
	assert(f1.hunger == f2.hunger);
	assert(z1.number == 99);
	assert(z1.number == z2.number);

	ecs::Entity new_entity2 = manager.CopyEntity<ZombieComponent>(entity);
	// Note the late manager.Refresh() call.
	assert(!new_entity2.IsIdenticalTo(entity));
	assert((entity.Has<FoodComponent, ZombieComponent>()));
	assert(new_entity2.Has<ZombieComponent>());
	assert(!new_entity2.Has<FoodComponent>());
	auto& z4 = entity.Get<ZombieComponent>();
	auto& z5 = new_entity2.Get<ZombieComponent>();
	auto& z6 = new_entity2.Get<ZombieComponent>();
	assert(z4.number == 99);
	assert(z4.number == z5.number);
	assert(z5.number == z6.number);

	manager.Refresh();

	assert(manager.Size() == 4); // entity, new_entity, new_entity2, new_entity_other.
	new_entity.Destroy();
	new_entity_other.Destroy();
	assert(manager.Size() == 4); // entity, new_entity, new_entity2, new_entity_other.
	manager.Refresh();
	assert(manager.Size() == 2); // entity, new_entity2.
	auto new_entity3 = manager.CreateEntity();
	assert(manager.Size() == 2); // entity, new_entity2, new_entity3.
	manager.Refresh();
	assert(manager.Size() == 3); // entity, new_entity2, new_entity3.

	manager.Clear();
	assert(manager.Size() == 0);
	manager.Reset();
	manager.Reserve(5);
	manager.CreateEntity();
	manager.CreateEntity();
	auto test_e = manager.CreateEntity();
	test_e.Add<ZombieComponent>(3);
	assert(manager.Size() == 0);
	manager.Refresh();
	assert(manager.Size() == 3);
	auto new_manager = manager;
	assert(new_manager != manager);
	assert(new_manager.Size() == 3);
	assert(manager.Size() == 3);

	TestHooks();

	std::cout << "All ECS tests passed!" << std::endl;

	return true;
}

struct ProfileTestComponent {
	int x{ 0 };
	int y{ 0 };
};

void ProfileECS() {
	std::cout << "Starting ECS profiling" << std::endl;

	std::size_t entity_count = 100000000;

	bool auto_for_loops = true;

	if (auto_for_loops) {
		ecs::Manager manager;

		auto start = std::chrono::high_resolution_clock::now();
		auto stop  = std::chrono::high_resolution_clock::now();
		for (std::size_t i = 0; i < entity_count; i++) {
			manager.CreateEntity();
		}

		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Creating " << entity_count << " entities took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;

		start = std::chrono::high_resolution_clock::now();
		manager.Refresh();
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Refreshing " << entity_count << " entities took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;

		start = std::chrono::high_resolution_clock::now();
		for (auto e : manager.Entities()) {
			e.Add<ProfileTestComponent>(3, 3);
		}
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Adding " << entity_count << " components took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;

		start = std::chrono::high_resolution_clock::now();
		for (auto [e, profile] : manager.EntitiesWith<ProfileTestComponent>()) {
			profile.x += 1;
		}
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Incrementing " << entity_count << " component members took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;

		start = std::chrono::high_resolution_clock::now();
		for (auto e : manager.Entities()) {
			e.Remove<ProfileTestComponent>();
		}
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Removing " << entity_count << " components took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;

		start = std::chrono::high_resolution_clock::now();
		for (auto e : manager.Entities()) {
			e.Add<ProfileTestComponent>(4, 4);
		}
		for (auto e : manager.Entities()) {
			e.Add<ProfileTestComponent>(5, 5);
		}
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "2x Readding " << entity_count << " components took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;
	} else {
		std::cout << "Using for each functions for profiling" << std::endl;

		ecs::Manager manager2;

		auto start = std::chrono::high_resolution_clock::now();
		auto stop  = std::chrono::high_resolution_clock::now();
		for (std::size_t i = 0; i < entity_count; i++) {
			manager2.CreateEntity();
		}
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Creating " << entity_count << " entities took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;

		start = std::chrono::high_resolution_clock::now();
		manager2.Refresh();
		stop = std::chrono::high_resolution_clock::now();
		std::cout << "Refreshing " << entity_count << " entities took "
				  << std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count()
				  << " ms" << std::endl;
	}

	std::cout << "Stopping ECS profiling" << std::endl;
}