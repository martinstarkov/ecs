#pragma once

#include "ecs/ecs.h"

struct HookCounters {
	inline static int construct1 = 0;
	inline static int construct2 = 0;
	inline static int update1	 = 0;
	inline static int destruct1	 = 0;

	static void Reset() {
		construct1 = construct2 = update1 = destruct1 = 0;
	}
};

struct HookComponent {
	HookComponent() = default;

	int irrelevant{ 0 };
};

void ConstructHook1(ecs::Entity) {
	HookCounters::construct1++;
}

void ConstructHook2(ecs::Entity) {
	HookCounters::construct2++;
}

void UpdateHook1(ecs::Entity) {
	HookCounters::update1++;
}

void DestructHook1(ecs::Entity) {
	HookCounters::destruct1++;
}

bool TestHooks() {
	HookCounters::Reset();

	ecs::Manager manager;

	auto construct_hook1 = manager.OnConstruct<HookComponent>().Connect<&ConstructHook1>();
	ECS_ASSERT(manager.HasOnConstruct<HookComponent>(construct_hook1), "");

	auto construct_hook2 = manager.OnConstruct<HookComponent>().Connect<&ConstructHook2>();
	ECS_ASSERT(manager.HasOnConstruct<HookComponent>(construct_hook2), "");

	auto update_hook1 = manager.OnUpdate<HookComponent>().Connect<&UpdateHook1>();
	ECS_ASSERT(manager.HasOnUpdate<HookComponent>(update_hook1), "");

	auto destruct_hook1 = manager.OnDestruct<HookComponent>().Connect<&DestructHook1>();
	ECS_ASSERT(manager.HasOnDestruct<HookComponent>(destruct_hook1), "");

	ecs::Entity e0 = manager.CreateEntity();
	ecs::Entity e1 = manager.CreateEntity();
	ecs::Entity e2 = manager.CreateEntity();
	ecs::Entity e3 = manager.CreateEntity();
	ecs::Entity e4 = manager.CreateEntity();

	e0.Add<HookComponent>();
	e1.Add<HookComponent>();

	ECS_ASSERT(HookCounters::construct1 == 2, "");
	ECS_ASSERT(HookCounters::construct2 == 2, "");

	manager.RemoveOnConstruct<HookComponent>(construct_hook2);
	ECS_ASSERT(!manager.HasOnConstruct<HookComponent>(construct_hook2), "");

	e2.Add<HookComponent>();
	e3.Add<HookComponent>();
	e4.Add<HookComponent>();

	ECS_ASSERT(HookCounters::construct1 == 5, "");
	ECS_ASSERT(HookCounters::construct2 == 2, "");

	e0.Update<HookComponent>();
	e1.Update<HookComponent>();
	ECS_ASSERT(HookCounters::update1 == 2, "");

	manager.RemoveOnUpdate<HookComponent>(update_hook1);
	ECS_ASSERT(!manager.HasOnUpdate<HookComponent>(update_hook1), "");

	e2.Update<HookComponent>();
	e3.Update<HookComponent>();
	e4.Update<HookComponent>();
	ECS_ASSERT(HookCounters::update1 == 2, "");

	e0.Remove<HookComponent>();
	e1.Remove<HookComponent>();
	ECS_ASSERT(HookCounters::destruct1 == 2, "");

	e2.Clear();
	ECS_ASSERT(HookCounters::destruct1 == 3, "");

	e3.Destroy();
	ECS_ASSERT(HookCounters::destruct1 == 4, "");

	manager.Refresh();

	manager.RemoveOnDestruct<HookComponent>(destruct_hook1);
	ECS_ASSERT(!manager.HasOnDestruct<HookComponent>(destruct_hook1), "");

	e4.Remove<HookComponent>();
	ECS_ASSERT(HookCounters::destruct1 == 4, "");

	return true;
}