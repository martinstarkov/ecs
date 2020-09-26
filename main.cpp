//#include "ECS.h" // 18.5 // 40 // 45
//#include "ECS2.h" // 9.3 // 19 // - // 10.8 // 16.3
#include "ECS3.h" // 7.8 // 15 // 6.8 // 10.3 // 7.4 // 17.9 // 14.6 // 14.8 // 15.4

#include <time.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>

#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

template <std::size_t I>
struct TPos { // 128 bytes total
	TPos(std::int64_t a = 0) : a{ a } {}
	friend std::ostream& operator<<(std::ostream& os, const TPos& obj) {
		os << "(" << obj.a << "," << obj.b << "," << obj.c << "," << obj.d << ")";
		return os;
	}
	std::int64_t a = 0; // 8 bytes
	std::int64_t b = 0; // 8 bytes
	std::int64_t c = 0; // 8 bytes
	std::int64_t d = 0; // 8 bytes

	std::int64_t e = 0; // 8 bytes
	std::int64_t f = 0; // 8 bytes
	std::int64_t g = 0; // 8 bytes
	std::int64_t h = 0; // 8 bytes
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

struct MySystem : public ecs::System<TPos<6>, TPos<7>> {
	void Update() {
		for (auto [entity, one, two] : entities) {
			one.a += 1;
			two.a += 1;
			LOG(one.a << ", " << two.a);
		}
	}
};

void test3() {
	ecs::Manager ecs;
	auto start = std::chrono::high_resolution_clock::now();
	for (auto i = 0; i < 20000; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<1>>(3);
		e.AddComponent<TPos<2>>(3);
		e.AddComponent<TPos<3>>(3);
		e.AddComponent<TPos<4>>(3);
		e.AddComponent<TPos<5>>(3);
		//e.RemoveComponent<TPos<3>>();
	}
	for (auto i = 0; i < 20000; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<6>>(5);
		e.AddComponent<TPos<7>>(5);
		e.AddComponent<TPos<8>>(5);
		//e.RemoveComponent<TPos<8>>();
		e.AddComponent<TPos<9>>(5);
		e.AddComponent<TPos<10>>(5);
	}
	auto stop_addition = std::chrono::high_resolution_clock::now();
	auto duration_addition = std::chrono::duration_cast<std::chrono::microseconds>(stop_addition - start);
	std::cout << "test3 took " << std::fixed << std::setprecision(1) << duration_addition.count() / 1000000.000 << " seconds to add all components" << std::endl;
	for (auto i = 0; i < 2; ++i) {
		ecs.ForEach<TPos<1>, TPos<5>>([&] (auto entity, auto& pos, auto& pos2) {
			//LOG_(pos.a << " -> ");
			pos.a += 1;
			//LOG(pos.a);
			pos.d += 1;
			pos2.a += 1;
			pos2.d += 1;
			ecs.ForEach<TPos<6>, TPos<10>>([&](auto entity, auto& pos3, auto& pos4) {
				pos3.a += 1;
				pos3.d += 1;
				pos4.a += 1;
				pos4.d += 1;
			}, false);
		}, false);
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
	ecs.Refresh();
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
	ecs.ForEachEntity([&](ecs::Entity entity) {
		LOG("entity: " << entity.GetComponent<TPos<0>>().a << ", isalive: " << ecs.IsAlive(entity));
		entity.Destroy();
		ecs.ForEachEntity([&](ecs::Entity entity) {
			LOG("entity: " << entity.GetComponent<TPos<0>>().a << ", isalive: " << ecs.IsAlive(entity));
		});
	});
	LOG("...");
	auto entities = ecs.GetEntities();
	for (auto e : entities) {
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
	ecs.Update<ExampleSystem>();
	LOG("Second update:");
	ecs.Update<ExampleSystem>();
	LOG("e4 has Tpos<0>: " << e4.HasComponent<TPos<0>>());
	//e4.GetComponent<TPos<0>>();
	e4.AddComponent<TPos<0>>(99);
	LOG("Third update:");
	ecs.Update<ExampleSystem>();
	LOG("My system update:");
	ecs.Update<MySystem>();
}

struct Test7System : public ecs::System<TPos<6>, TPos<7>> {
	void Update() {
		for (auto [entity, one, two] : entities) {
			one.a += 1;
			two.a += 1;
		}
	}
};

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
	auto stop_addition = std::chrono::high_resolution_clock::now();
	auto duration_addition = std::chrono::duration_cast<std::chrono::microseconds>(stop_addition - start);
	std::cout << "test7 took " << std::fixed << std::setprecision(1) << duration_addition.count() / 1000000.000 << " seconds to add all components" << std::endl;
	// 10 million update cycles
	for (auto i = 0; i < 10000000; ++i) {
		ecs.Update<Test7System>();
	}
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration_get = std::chrono::duration_cast<std::chrono::microseconds>(stop - stop_addition);
	std::cout << "test7 get component loop time = " << std::fixed << std::setprecision(1) << duration_get.count() / 1000000.000 << std::endl;
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	std::cout << "test7 total execution_time = " << std::fixed << std::setprecision(1) << duration.count() / 1000000.000 << std::endl;
}

struct Test8System : public ecs::System<TPos<0>, TPos<1>> {
	void Update() {
		for (auto [entity, zero, one] : entities) {
			LOG("Entity " << entity.GetId() << " has a zero and a one : [" << zero.a << ", " << one.a << "]");
		}
	}
};

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
	ecs.Update<Test8System>();
	e2.RemoveComponent<TPos<0>>();
	LOG("Update after removal: ");
	ecs.Update<Test8System>();
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
	assert((ecs.GetEntities().size()) == 4);
	assert((ecs.GetEntitiesWith<TPos<1>>().size()) == 3);
	//assert((ecs.GetComponentTuple<TPos<1>, TPos<2>>().size()) == 2);
	auto [one, two] = e3.GetComponents<TPos<1>, TPos<2>>();
	ecs.ForEach<TPos<1>>([](auto entity, auto& pos_one) {
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
	});
}

int main() {
	test7();
	//if (true) {
	//	ecs::EntityId entities = 30000;
	//	ecs::Manager manager(entities, 20);
	//	std::size_t loops = 20;
	//	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	//	assign(manager, entities);
	//	LOG("ASSIGNEMT COMPLETED!");
	//	LOG("TIMING LOOPS!");
	//	auto start = std::chrono::high_resolution_clock::now();
	//	/*for (std::size_t i = 0; i < loops; ++i) {
	//		update(manager);
	//	}*/
	//	LOG("LOOPS COMPLETED!");
	//	using namespace std::chrono;
	//	unsigned frame_count_per_second = 0;
	//	auto prev_time_in_seconds = time_point_cast<seconds>(system_clock::now());
	//	size_t counter = 0;
	//	while (counter <= 10) {
	//		for (std::size_t i = 0; i < loops; ++i) {
	//			update(manager);
	//		}
	//		auto time_in_seconds = time_point_cast<seconds>(system_clock::now());
	//		++frame_count_per_second;
	//		if (time_in_seconds > prev_time_in_seconds) {
	//			std::cerr << frame_count_per_second << " frames per second\n";
	//			frame_count_per_second = 0;
	//			prev_time_in_seconds = time_in_seconds;
	//			++counter;
	//		}
	//	}
	//	auto stop = std::chrono::high_resolution_clock::now();
	//	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	//	std::cout << "execution_time = " << std::fixed << std::setprecision(1) << duration.count() / 1000000.000 << std::endl;
	//}
	// 10000, 1 mil loops, 611, 643s, Manager 3
	// 10000, 1 mil loops, 591, 592s, Manager 5

	// 100, 10 mil loops, 61, 61, 61s, Manager 3
	// 100, 10 mil loops, 56, 59, 61s, Manager 5

	// 1 mil, 1k loops, 78.6s, Manager 5
	// 1 mil, 1k loops, 75.7s, Manager 3

	// 1 mil, 10 loops, 96s, Manager 4 custom allocator adding and removing components
	// 1 mil, 10 loops, 77s, Manager 3 vector pairs adding and removing components

	// 1 mil, 100 loops, 8.6s, Manager 5
	// 1 mil, 100 loops, 8.2s, Manager 3

	// 1 mil, 110 loops, 11.9s, Manager 5
	// 1 mil, 100 loops, 12s, Manager 3

	// 1 mil, 1k loops, 72s, Manager 5 custom allocator
	// 1 mil, 1k loops, 102s, Manager 4 custom allocator
	// 1 mil, 1k loops, 82s, Manager 3 vector pairs

	// 1 mil, 100 loops, 7.1s, Manager 5 custom allocator
	// 1 mil, 100 loops, 10.3s, Manager 4 custom allocator
	// 1 mil, 100 loops, 8.1s, Manager 3 vector pairs

	// 1 mil, 1k loops, 87, 92, 92s, Manager 4 custom allocator
	// 1 mil, 1k loops, 75, 76s, Manager 3 vector pairs

	// 1k, 1 mil loops, 43s, Manager 4 custom allocator 
	// 1k, 1 mil loops, 45s, Manager 3 vector pairs

	// 1 mil, 1k loops, 501mb, 88s, Manager 4 custom allocator
	// 1 mil, 1k loops, 382mb, 105s, Manager 3 vector pairs
	// 1 mil, 1k loops, 662mb, 176s, Manager 3 regular map

	// 1 mil, 1k loops, 88s, Manager 4 custom allocator
	// 1 mil, 1k loops, 104s, Manager 3 vector pairs

	// 100k, 10k loops, 89s, Manager 4 custom allocator 
	// 100k, 10k loops, 105s, Manager 3 vector pairs
	// 100k, 10k loops, 176s, Manager 3 regular map

	// 10k, 100k loops, 81s, Manager 4 custom allocator 
	// 10k, 100k loops, 82s, Manager 3 vector pairs
	// 10k, 100k loops, 169s, Manager 3 regular map

	// 10k, 10k loops, 8.7s, Manager 4 custom allocator 
	// 10k, 10k loops, 8.9s, Manager 3 vector pairs
	// 10k, 10k loops, 17s, Manager 3 regular map

	// 100k, 1k loops, 8.7s, Manager 4 custom allocator 
	// 100k, 1k loops, 9.0s, Manager 3 vector pairs

	//std::cin.get();
	return 0;
}