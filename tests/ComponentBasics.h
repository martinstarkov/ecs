#pragma once

#include "common.h"

template <typename T>
struct TestComponent {
	TestComponent(T value) : value{ value } {}
	friend std::ostream& operator<<(std::ostream& os, const TestComponent& obj) {
		os << obj.value;
		return os;
	}
	T value;
};

static void ComponentBasics() {

	ecs::Manager manager;
	auto e0 = manager.CreateEntity();
	manager.Refresh();
	auto e1 = manager.CreateEntity();
	e1.AddComponent<TestComponent<int>>(1);
	assert(e1.HasComponent<TestComponent<int>>());
	e1.AddComponent<TestComponent<double>>(1.0);
	assert(e1.HasComponent<TestComponent<int>>() && e1.HasComponent<TestComponent<double>>());
	assert((e1.HasComponents<TestComponent<int>, TestComponent<double>>()));
	auto e2 = manager.CreateEntity();
	manager.Refresh();
	auto e3 = manager.CreateEntity();
	auto e4 = manager.CreateEntity();
	e4.AddComponent<TestComponent<int>>(4);
	assert(e4.HasComponent<TestComponent<int>>());
	e4.AddComponent<TestComponent<double>>(4.0);
	assert(e4.HasComponent<TestComponent<int>>() && e4.HasComponent<TestComponent<double>>());
	assert((e4.HasComponents<TestComponent<int>, TestComponent<double>>()));
	e4.RemoveComponent<TestComponent<int>>();
	assert(!e4.HasComponent<TestComponent<int>>() && e4.HasComponent<TestComponent<double>>());
	e4.RemoveComponent<TestComponent<double>>();
	assert(!e4.HasComponent<TestComponent<int>>() && !e4.HasComponent<TestComponent<double>>());
	auto e5 = manager.CreateEntity();
	manager.Refresh();
	e1.Destroy();
	assert(e1.HasComponent<TestComponent<int>>() && e1.HasComponent<TestComponent<double>>());
	e3.Destroy();
	e5.Destroy();
	manager.Refresh();
	//assert(!e1.HasComponent<TestComponent<int>>() && !e1.HasComponent<TestComponent<double>>());
	auto e3_new = manager.CreateEntity();
	manager.Refresh();
	auto e1_new = manager.CreateEntity();
	auto e5_new = manager.CreateEntity();
	manager.Refresh();


	LOG("Component basics passed");
}