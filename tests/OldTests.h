#pragma once

#include "common.h"

template <std::size_t I>
struct TPos {
	TPos(std::int64_t a = 0) : a{ a } {}
	friend std::ostream& operator<<(std::ostream& os, const TPos& obj) {
		os << "(" << obj.a << "," << obj.b << "," << obj.c << "," << obj.d << ")";
		return os;
	}
	std::int64_t a = 0;
	std::int64_t b = 0;
	std::int64_t c = 0;
	std::int64_t d = 0;

	std::int64_t e = 0;
	std::int64_t f = 0;
	std::int64_t g = 0;
	std::int64_t h = 0;
};

struct ExampleSystem : public ecs::System<TPos<0>, TPos<1>> {
	void Update() {
		for (auto [entity, pos, vel] : entities) {
			entity.RemoveComponent<TPos<0>>();
			for (auto [entity2, pos2, vel2] : entities) {
				LOG("inner: " << pos2.a << "," << vel2.a);
			}
			LOG(pos.a << "," << vel.a);
		}
	}
};

template <typename ...T>
struct Testing {
public:
	using Components = ecs::internal::type_traits::parameter_pack<T...>;
	std::vector<std::tuple<int, T&...>> entities;
};

struct MySystem : public ecs::System<TPos<6>, TPos<7>> {
	void Update() {
		for (auto [test, one, two] : entities) {
			one.a += 1;
			two.a += 1;
			LOG(one.a << ", " << two.a);
		}
	}
};

struct Test7System : public ecs::System<TPos<6>, TPos<7>> {
	void Update() {
		for (auto [entity, one, two] : entities) {
			one.a += 1;
			two.a += 1;
		}
	}
};

struct Test8System : public ecs::System<TPos<0>, TPos<1>> {
	void Update() {
		for (auto [entity, zero, one] : entities) {
			LOG("Entity " << " has a zero and a one : [" << zero.a << ", " << one.a << "]");
		}
	}
};

void test3() {
	ecs::Manager ecs;
	auto start = std::chrono::high_resolution_clock::now();
	for (auto i = 0; i < 200; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<1>>(3);
		e.AddComponent<TPos<2>>(3);
		e.AddComponent<TPos<3>>(3);
		e.AddComponent<TPos<4>>(3);
		e.AddComponent<TPos<5>>(3);
	}
	for (auto i = 0; i < 200; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<6>>(5);
		e.AddComponent<TPos<7>>(5);
		e.AddComponent<TPos<8>>(5);
		e.AddComponent<TPos<9>>(5);
		e.AddComponent<TPos<10>>(5);
	}
	ecs.Refresh();
	auto stop_addition = std::chrono::high_resolution_clock::now();
	auto duration_addition = std::chrono::duration_cast<std::chrono::microseconds>(stop_addition - start);
	std::cout << "test3 took " << std::fixed << std::setprecision(1) << duration_addition.count() / 1000000.000 << " seconds to add all components" << std::endl;
	for (auto i = 0; i < 2; ++i) {
		//ecs.ForEach<TPos<1>, TPos<5>>([&](auto entity, auto& pos, auto& pos2) {
		//	//LOG_(pos.a << " -> ");
		//	pos.a += 1;
		//	//LOG(pos.a);
		//	pos.d += 1;
		//	pos2.a += 1;
		//	pos2.d += 1;
		//	ecs.ForEach<TPos<6>, TPos<10>>([&](auto entity, auto& pos3, auto& pos4) {
		//		pos3.a += 1;
		//		pos3.d += 1;
		//		pos4.a += 1;
		//		pos4.d += 1;
		//	});
		//});
	}
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration_get = std::chrono::duration_cast<std::chrono::microseconds>(stop - stop_addition);
	std::cout << "test3 get component loop time = " << std::fixed << std::setprecision(1) << duration_get.count() / 1000000.000 << std::endl;
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	std::cout << "test3 total execution_time = " << std::fixed << std::setprecision(1) << duration.count() / 1000000.000 << std::endl;
}

void test4() {
	ecs::Manager ecs;
	auto e1 = ecs.CreateEntity();
	auto e2 = ecs.CreateEntity();
	e1.AddComponent<TPos<0>>(2);
	e1.AddComponent<TPos<1>>(3);
	e2.AddComponent<TPos<1>>(5);
	assert(e1.HasComponent<TPos<0>>());
	assert(e1.GetComponent<TPos<0>>().a == 2);
	//e1.RemoveComponents<TPos<0>, TPos<1>>();
	//e1.RemoveComponent<TPos<0>>();
	e2.AddComponent<TPos<0>>(4);
	//LOG(e1.GetComponent<TPos<0>>().a);
	//assert(e1.HasComponent<TPos<0>>());
	//assert(e1.HasComponent<TPos<1>>());
	assert((e1.HasComponents<TPos<0>, TPos<1>>()));
	auto [one, two] = e1.GetComponents<TPos<0>, TPos<1>>();
	LOG(one.a << "," << two.a);
}

void test5() {
	ecs::Manager ecs;
	auto e1 = ecs.CreateEntity();
	auto e2 = ecs.CreateEntity();
	auto e3 = ecs.CreateEntity();
	e1.AddComponent<TPos<0>>(1);
	e2.AddComponent<TPos<0>>(2);
	e3.AddComponent<TPos<0>>(3);
	/*ecs.ForEachEntity([&](ecs::Entity entity) {
		LOG("entity: " << entity.GetComponent<TPos<0>>().a << ", isalive: " << ecs.IsAlive(entity));
		entity.Destroy();
		ecs.ForEachEntity([&](ecs::Entity entity) {
			LOG("entity: " << entity.GetComponent<TPos<0>>().a << ", isalive: " << ecs.IsAlive(entity));
		});
	});*/
	LOG("...");
	ecs.Refresh();
	auto entities = ecs.GetEntities();
	for (auto& e : entities) {
		LOG("entity is 3: " << (e == e3));
	}
}

void test6() {
	ecs::Manager ecs;
	ecs.AddSystem<MySystem>();
	auto e1 = ecs.CreateEntity();
	auto e2 = ecs.CreateEntity();
	auto e3 = ecs.CreateEntity();
	auto e4 = ecs.CreateEntity();
	auto e5 = ecs.CreateEntity();
	e2.AddComponent<TPos<0>>(1);
	e3.AddComponent<TPos<0>>(2);
	e3.AddComponent<TPos<1>>(3);
	e4.AddComponent<TPos<0>>(4);
	ecs.AddSystem<ExampleSystem>();
	e4.AddComponent<TPos<1>>(5);
	e4.AddComponent<TPos<6>>(6);
	e5.AddComponent<TPos<6>>(69);
	e5.AddComponent<TPos<7>>(699);
	LOG("First update:");
	ecs.Refresh();
	ecs.UpdateSystem<ExampleSystem>();
	LOG("Second update:");
	ecs.UpdateSystem<ExampleSystem>();
	LOG("e4 has Tpos<0>: " << e4.HasComponent<TPos<0>>());
	//e4.GetComponent<TPos<0>>();
	e4.AddComponent<TPos<0>>(99);
	LOG("Third update:");
	ecs.UpdateSystem<ExampleSystem>();
	LOG("My system update:");
	ecs.UpdateSystem<MySystem>();
}

void test7() {
	ecs::Manager ecs;
	ecs.AddSystem<Test7System>();
	auto start = std::chrono::high_resolution_clock::now();
	for (auto i = 0; i < 100; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<1>>(3);
		e.AddComponent<TPos<2>>(3);
		e.AddComponent<TPos<6>>(3);
		e.AddComponent<TPos<7>>(3);
		e.AddComponent<TPos<5>>(3);
		e.RemoveComponent<TPos<2>>();
	}
	ecs.Refresh();
	auto stop_addition = std::chrono::high_resolution_clock::now();
	auto duration_addition = std::chrono::duration_cast<std::chrono::microseconds>(stop_addition - start);
	std::cout << "test7 took " << std::fixed << std::setprecision(1) << duration_addition.count() / 1000000.000 << " seconds to add all components" << std::endl;
	// 10 update cycles
	for (auto i = 0; i < 10; ++i) {
		ecs.UpdateSystem<Test7System>();
	}
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration_get = std::chrono::duration_cast<std::chrono::microseconds>(stop - stop_addition);
	std::cout << "test7 get component loop time = " << std::fixed << std::setprecision(1) << duration_get.count() / 1000000.000 << std::endl;
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	std::cout << "test7 total execution_time = " << std::fixed << std::setprecision(1) << duration.count() / 1000000.000 << std::endl;
}

void test8() {
	ecs::Manager ecs;
	ecs.AddSystem<Test8System>();
	auto e1 = ecs.CreateEntity();
	auto e2 = ecs.CreateEntity();
	auto e3 = ecs.CreateEntity();
	auto e4 = ecs.CreateEntity();
	e1.AddComponent<TPos<0>>(69);

	e2.AddComponent<TPos<0>>(70);
	e2.AddComponent<TPos<1>>(71);

	e3.AddComponent<TPos<0>>(72);
	e3.AddComponent<TPos<1>>(73);
	e3.AddComponent<TPos<2>>(74);

	e4.AddComponent<TPos<1>>(75);
	e4.AddComponent<TPos<2>>(76);
	LOG("Update before removal: ");
	ecs.Refresh();
	ecs.UpdateSystem<Test8System>();
	e2.RemoveComponent<TPos<0>>();
	LOG("Update after removal: ");
	ecs.UpdateSystem<Test8System>();
	LOG("Completed");
}

void test9() {
	ecs::Manager ecs;
	auto e1 = ecs.CreateEntity();
	auto e2 = ecs.CreateEntity();
	auto e3 = ecs.CreateEntity();
	auto e4 = ecs.CreateEntity();
	e1.AddComponent<TPos<0>>(69);
	ecs::Entity test = ecs::null;
	bool hi = test == ecs::null;
	LOG(hi);
	e2.AddComponent<TPos<0>>(70);
	e2.AddComponent<TPos<1>>(71);

	e3.AddComponent<TPos<0>>(72);
	e3.AddComponent<TPos<1>>(73);
	e3.AddComponent<TPos<2>>(74);
	e4.AddComponent<TPos<1>>(75);
	e4.AddComponent<TPos<2>>(76);
	ecs.Refresh();
	assert((ecs.GetEntities().size()) == 4);
	//assert((ecs.GetEntitiesWith<TPos<1>>().size()) == 3);
	//assert((ecs.GetComponentTuple<TPos<1>, TPos<2>>().size()) == 2);
	auto [one, two] = e3.GetComponents<TPos<1>, TPos<2>>();
	/*ecs.ForEach<TPos<1>>([](auto entity, auto& pos_one) {
		LOG_(pos_one.a << " -> ");
		pos_one.a += 1;
		LOG(pos_one.a);
	});
	ecs.ForEachEntity([](auto entity) {
		entity.AddComponent<TPos<0>>(5);
		if (entity.HasComponent<TPos<0>>()) {
			LOG("Entity " << entity.GetId() << " has TPos<0>");
		}
		if (entity.GetId() > 2) {
			entity.RemoveComponent<TPos<0>>();
		}
	});
	ecs.ForEach<TPos<0>>([](auto entity, auto& pos_zero) {
		LOG("Entity " << entity.GetId() << " TPos<0> after: " << pos_zero.a);
	});*/
}

void test10() {
	ecs::Manager ecs1;
	ecs::Manager ecs2;
	auto e11 = ecs1.CreateEntity();
	e11.AddComponent<TPos<0>>(1);
	auto e12 = ecs1.CreateEntity();
	auto e21 = ecs2.CreateEntity();
	e12.AddComponent<TPos<0>>(2);
	auto e22 = ecs2.CreateEntity();
	e21.AddComponent<TPos<1>>(11);
	e22.AddComponent<TPos<1>>(10);
	e21.AddComponent<TPos<0>>(3);
	e22.AddComponent<TPos<0>>(4);
	LOG(e11.GetComponent<TPos<0>>().a);
	LOG(e12.GetComponent<TPos<0>>().a);
	LOG(e21.GetComponent<TPos<0>>().a);
	LOG(e22.GetComponent<TPos<0>>().a);
	e22.RemoveComponent<TPos<0>>();
	e21.RemoveComponent<TPos<1>>();
	assert(!e22.HasComponent<TPos<0>>());
	assert(!e21.HasComponent<TPos<1>>());
	e11.AddComponent<TPos<1>>(13);
	e12.AddComponent<TPos<1>>(12);
	e21.AddComponent<TPos<1>>(11);
	LOG(e11.GetComponent<TPos<1>>().a);
	LOG(e12.GetComponent<TPos<1>>().a);
	LOG(e21.GetComponent<TPos<1>>().a);
	LOG(e22.GetComponent<TPos<1>>().a);
	assert(e11.HasComponent<TPos<1>>());
	assert(e12.HasComponent<TPos<1>>());
	assert(e21.HasComponent<TPos<1>>());
	assert(e22.HasComponent<TPos<1>>());
	assert(e11.HasComponent<TPos<0>>());
	assert(e12.HasComponent<TPos<0>>());
	assert(e21.HasComponent<TPos<0>>());
}