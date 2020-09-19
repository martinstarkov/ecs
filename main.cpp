//#include "ECS.h" // 18.5 // 40
//#include "ECS2.h" // 9.3 // 19 // - // 10.8
#include "ECS3.h" // 7.8 // 15 // 6.8 // 10.3 // 7.4

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

//void test1() {
//	ecs::Manager ecs;
//	ecs::Entity e = ecs.CreateEntity();
//	LOG("e id: " << e.GetId());
//	LOG("---------");
//	LOG("e component count: " << e.ComponentCount());
//	LOG("---------");
//	LOG("Adding Position, Velocity, Euler");
//	e.AddComponent<Position>(1, 1);
//	e.AddComponent<Velocity>(2, 2);
//	e.AddComponent<Euler>(3, 4, 5, 6);
//	auto e2 = ecs.CreateEntity();
//	e2.AddComponent<Position>(2, 2);
//	e2.AddComponent<Velocity>(20, 20);
//	e2.AddComponent<RandomThing>(20);
//	ecs.ForEachWrapper<Velocity, Position>([&](auto entity, auto vel, auto pos) {
//		LOG("Entity: " << entity.GetId() << ", Velocity: " << vel.Get());
//		ecs.ForEachWrapper<Velocity>([&](auto entity2, auto random) {
//			//entity.RemoveComponent<Velocity>();
//			//entity2.RemoveComponent<RandomThing>();
//			//entity2.Destroy();
//			//ecs.Refresh();
//			LOG("Inner Entity: " << entity2.GetId() << ", RandomThing: " << random.Get());
//		});
//	});
//	LOG("---------");
//	LOG("e component count: " << e.ComponentCount());
//	LOG("---------");
//	LOG("Removing Velocity");
//	e.RemoveComponent<Velocity>();
//	//e.Destroy();
//	ecs.ForEach<Velocity>([&](auto entity, auto& vel) {
//		LOG("Entity: " << entity.GetId() << ", Velocity: " << vel);
//	});
//	LOG("---------");
//	LOG("e component count: " << e.ComponentCount());
//	LOG("---------");
//	LOG("Adding OtherThing");
//	e.AddComponent<OtherThing>(7, 8, 7, 8);
//	LOG("---------");
//	LOG("e component count: " << e.ComponentCount());
//	//e.RemoveComponent<OtherThing>();
//	auto [p, v, o] = e.GetComponentWrappers<Position, Euler, OtherThing>();
//	LOG("e components: " << p << "," << v << "," << o);
//	LOG("Destroying Entity");
//	e.Destroy();
//	LOG("---------");
//	LOG("Adding euler");
//	LOG("---------");
//}
//
//void test2() {
//	ecs::Manager ecs;
//	for (auto i = 0; i < 100; ++i) {
//		auto e = ecs.CreateEntity();
//		e.AddComponent<Position>(i, i);
//		e.AddComponent<Velocity>();
//		e.AddComponent<OtherThing>();
//		e.AddComponent<RandomThing>();
//		e.AddComponent<Euler>();
//	}
//	auto block = ecs.GetPoolHandlerBlock();
//	auto e2 = ecs.GetEntity(2);
//	e2.RemoveComponent<OtherThing>();
//	ecs.GetEntity(3).Destroy();
//	ecs.Refresh();
//	ecs.GetEntity(4).Destroy();
//	ecs.Refresh();
//	ecs.GetEntity(5).Destroy();
//	ecs.Refresh();
//	ecs.GetEntity(6).Destroy();
//	ecs.Refresh();
//	ecs.GetEntity(7).Destroy();
//	ecs.Refresh();
//	ecs.GetEntity(8).Destroy();
//	ecs.Refresh();
//	ecs.GetEntity(9).Destroy();
//	ecs.Refresh();
//}

void test3() {
	ecs::Manager ecs;
	auto start = std::chrono::high_resolution_clock::now();
	for (auto i = 0; i < 10000; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<1>>(3);
		e.AddComponent<TPos<2>>(3);
		e.AddComponent<TPos<3>>(3);
		e.RemoveComponent<TPos<3>>();
		e.AddComponent<TPos<4>>(3);
		e.AddComponent<TPos<5>>(3);
	}
	for (auto i = 0; i < 10000; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<6>>(4);
		e.AddComponent<TPos<7>>(4);
		e.AddComponent<TPos<8>>(4);
		e.RemoveComponent<TPos<8>>();
		e.AddComponent<TPos<9>>(4);
		e.AddComponent<TPos<10>>(4);
	}
	auto stop_addition = std::chrono::high_resolution_clock::now();
	auto duration_addition = std::chrono::duration_cast<std::chrono::microseconds>(stop_addition - start);
	std::cout << "test3 took " << std::fixed << std::setprecision(1) << duration_addition.count() / 1000000.000 << " seconds to add all components" << std::endl;
	for (auto i = 0; i < 1; ++i) {
		ecs.ForEach<TPos<1>, TPos<5>>([&] (auto& pos, auto& pos2) {
			//LOG_(pos.a << " -> ");
			pos.a += 1;
			//LOG(pos.a);
			pos.d += 1;
			pos2.a += 1;
			pos2.d += 1;
			ecs.ForEach<TPos<6>, TPos<10>>([&](auto& pos3, auto& pos4) {
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
	e1.AddComponent<TPos<0>>(2);
	e1.AddComponent<TPos<1>>(3);
	assert(e1.HasComponent<TPos<0>>());
	assert(e1.GetComponent<TPos<0>>().a == 2);
	//e1.RemoveComponents<TPos<0>, TPos<1>>();
	//assert(e1.HasComponent<TPos<0>>());
	//assert(e1.HasComponent<TPos<1>>());
	assert((e1.HasComponents<TPos<0>, TPos<1>>()));
	e1.Destroy();
	ecs.Refresh();
	auto [one, two] = e1.GetComponents<TPos<0>, TPos<1>>();
	LOG(one.a << "," << two.a);
}

int main() {
	test4();
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