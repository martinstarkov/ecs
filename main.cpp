#include "ECS.h"

#include <time.h>
#include <chrono>
#include <thread>
#include <iomanip>

#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

struct Position { // 8 bytes total
	Position(std::uint32_t x, std::uint32_t y) : x(x), y(y) {}
	std::uint32_t x, y;
	friend std::ostream& operator<<(std::ostream& os, const Position& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Velocity { // 16 bytes total
	Velocity(std::uint64_t x, std::uint64_t y) : x(x), y(y) {}
	std::uint64_t x, y;
	friend std::ostream& operator<<(std::ostream& os, const Velocity& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct OtherThing { // 32 bytes total
	OtherThing(std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t w) : x(x), y(y), z{ z }, w{ w } {}
	std::uint64_t x, y, z, w;
	friend std::ostream& operator<<(std::ostream& os, const OtherThing& obj) {
		os << "(" << obj.x << "," << obj.y << "," << obj.z << "," << obj.w << ")";
		return os;
	}
};

struct Euler { // 32 bytes total
	Euler(std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t w) : x(x), y(y), z{ z }, w{w} {}
	std::uint64_t x, y, z, w;
	friend std::ostream& operator<<(std::ostream& os, const Euler& obj) {
		os << "(" << obj.x << "," << obj.y << "," << obj.z << "," << obj.w << ")";
		return os;
	}
};
//void update(ecs::Manager& manager) {
//	static auto& cache = manager.AddCache<Position, Velocity>();
//	for (auto [id, pos, vel, col] : cache.GetEntities()) {
//		manager.AddComponent<int>(id, 1);
//		pos.x += 1;
//		vel.y += 1;
//		col.x += 1;
//	}
//	manager.Update();
//}

int main() {
	ecs::Manager ecs;
	ecs::Entity e = ecs.CreateEntity();
	LOG("e id: " << e.GetId());
	LOG("---------");
	LOG("e component count: " << e.ComponentCount());
	LOG("---------");
	LOG("Adding Position, Velocity, Euler");
	e.AddComponent<Position>(1, 1);
	e.AddComponent<Velocity>(2, 2);
	e.AddComponent<Euler>(3, 4, 5, 6);
	ecs.CreateEntity().AddComponent<Velocity>(20, 20);
	auto& cache1 = ecs.AddCache<Velocity>();
	for (auto [entity, velocity] : cache1.GetEntities()) {
		LOG("Entity: " << entity.GetId() << ", Velocity: " << velocity);
	}
	LOG("---------");
	LOG("e component count: " << e.ComponentCount());
	LOG("---------");
	LOG("Removing Velocity");
	e.Destroy();
	ecs.Update();
	for (auto [entity, velocity] : cache1.GetEntities()) {
		LOG("Entity: " << entity.GetId() << ", Velocity: " << velocity);
	}
	LOG("---------");
	LOG("e component count: " << e.ComponentCount());
	LOG("---------");
	LOG("Adding OtherThing");
	e.AddComponent<OtherThing>(7, 8, 7, 8);
	LOG("---------");
	LOG("e component count: " << e.ComponentCount());
	auto [p, v] = e.GetComponents<Position, Euler>();
	LOG("e components: " << p << "," << v);
	LOG("Destroying Entity");
	e.Destroy();
	LOG("---------");
	LOG("Adding euler");
	LOG("---------");
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