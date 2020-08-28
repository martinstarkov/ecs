#include "ECS.h"

#include <time.h>
#include <chrono>
#include <thread>
#include <iomanip>

template <std::size_t I>
struct Test {
	Test(double x, double y) : x(x), y(y) {}
	double x, y;
	friend std::ostream& operator<<(std::ostream& os, const Test<I>& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Position {
	Position(double x, double y) : x(x), y(y) {}
	double x, y;
	friend std::ostream& operator<<(std::ostream& os, const Position& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Velocity {
	Velocity(double x, double y) : x(x), y(y) {}
	double x, y;
	friend std::ostream& operator<<(std::ostream& os, const Velocity& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Collision {
	Collision(double x, double y) : x(x), y(y) {}
	double x, y;
	friend std::ostream& operator<<(std::ostream& os, const Collision& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

void assign(ecs::Manager6& manager, ecs::EntityId entities) {
	for (ecs::EntityId counter = 0; counter < entities; ++counter) {
		ecs::EntityId i = manager.CreateEntity();
		manager.AddComponent<Position>(i, 50, 50);
		manager.AddComponent<Velocity>(i, 25, 25);
		manager.AddComponent<Collision>(i, 75, 75);
	}
}

void update(ecs::Manager6& manager) {
	manager.ForEach<Position, Velocity, Collision>([](auto& id, auto& pos, auto& vel, auto& col) {
		if (id == 1) {
			pos.x += 1;
			vel.y += 1;
			col.x += 1;
		}
	});
	/*manager.ForEach<Test<1>, Test<2>, Test<3>, Test<4>, Test<5>, Test<6>, Test<7>, Test<8>, Test<9>, Test<10>, Test<11>, Test<12>, Test<13>, Test<14>, Test<15>, Test<16>, Test<17>, Test<18>, Test<19>, Test<20>>([](auto& c1, auto& c2, auto& c3, auto& c4, auto& c5, auto& c6, auto& c7, auto& c8, auto& c9, auto& c10, auto& c11, auto& c12, auto& c13, auto& c14, auto& c15, auto& c16, auto& c17, auto& c18, auto& c19, auto& c20) {
		c1.x += 1;
		c2.x += 1;
		c3.x += 1;
		c4.x += 1;
		c5.x += 1;
		c6.x += 1;
		c7.x += 1;
		c8.x += 1;
		c9.x += 1;
		c10.x += 1;
		c11.x += 1;
		c12.x += 1;
		c13.x += 1;
		c14.x += 1;
		c15.x += 1;
		c16.x += 1;
		c17.x += 1;
		c18.x += 1;
		c19.x += 1;
		c20.x += 1;
	});*/
	//for (ecs::EntityId i = ecs::first_valid_entity; i < manager.EntityCount(); ++i) {
	//	/*auto [c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11, c12, c13, c14, c15, c16, c17, c18, c19, c20] = manager.GetComponents<Test<1>, Test<2>, Test<3>, Test<4>, Test<5>, Test<6>, Test<7>, Test<8>, Test<9>, Test<10>, Test<11>, Test<12>, Test<13>, Test<14>, Test<15>, Test<16>, Test<17>, Test<18>, Test<19>, Test<20>>(i);*/
	//	/*auto& c1 = manager.GetComponent<Test<1>>(i);
	//	auto& c2 = manager.GetComponent<Test<2>>(i);
	//	auto& c3 = manager.GetComponent<Test<3>>(i);
	//	auto& c4 = manager.GetComponent<Test<4>>(i);
	//	auto& c5 = manager.GetComponent<Test<5>>(i);
	//	auto& c6 = manager.GetComponent<Test<6>>(i);
	//	auto& c7 = manager.GetComponent<Test<7>>(i);
	//	auto& c8 = manager.GetComponent<Test<8>>(i);
	//	auto& c9 = manager.GetComponent<Test<9>>(i);
	//	auto& c10 = manager.GetComponent<Test<10>>(i);
	//	auto& c11 = manager.GetComponent<Test<11>>(i);
	//	auto& c12 = manager.GetComponent<Test<12>>(i);
	//	auto& c13 = manager.GetComponent<Test<13>>(i);
	//	auto& c14 = manager.GetComponent<Test<14>>(i);
	//	auto& c15 = manager.GetComponent<Test<15>>(i);
	//	auto& c16 = manager.GetComponent<Test<16>>(i);
	//	auto& c17 = manager.GetComponent<Test<17>>(i);
	//	auto& c18 = manager.GetComponent<Test<18>>(i);
	//	auto& c19 = manager.GetComponent<Test<19>>(i);
	//	auto& c20 = manager.GetComponent<Test<20>>(i);*/
	//	/*c1.x += 1;
	//	c2.x += 1;
	//	c3.x += 1;
	//	c4.x += 1;
	//	c5.x += 1;
	//	c6.x += 1;
	//	c7.x += 1;
	//	c8.x += 1;
	//	c9.x += 1;
	//	c10.x += 1;
	//	c11.x += 1;
	//	c12.x += 1;
	//	c13.x += 1;
	//	c14.x += 1;
	//	c15.x += 1;
	//	c16.x += 1;
	//	c17.x += 1;
	//	c18.x += 1;
	//	c19.x += 1;
	//	c20.x += 1;*/
	//}
}

int main() {
	ecs::EntityId entities = 1000000;
	ecs::Manager6 manager(entities, 20);
	std::size_t loops = 100;
	// 1 mil, 1k, 758mb, 8.9, 9.0, M5
	// 1 mil, 1k, 941mb, 9.2, 9.3, M6
	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	assign(manager, entities);
	LOG("ASSIGNEMT COMPLETED!");
	LOG("TIMING LOOPS!");
	auto start = std::chrono::high_resolution_clock::now();
	for (std::size_t i = 0; i < loops; ++i) {
		update(manager);
	}
	LOG(manager.GetComponent<Position>(1));
	LOG(manager.GetComponent<Velocity>(1));
	LOG("LOOPS COMPLETED!");
	/*using namespace std::chrono;
	unsigned frame_count_per_second = 0;
	auto prev_time_in_seconds = time_point_cast<seconds>(system_clock::now());
	size_t counter = 0;
	while (counter <= 10) {
		for (std::size_t i = 0; i < loops; ++i) {
			update(manager);
		}
		auto time_in_seconds = time_point_cast<seconds>(system_clock::now());
		++frame_count_per_second;
		if (time_in_seconds > prev_time_in_seconds) {
			std::cerr << frame_count_per_second << " frames per second\n";
			frame_count_per_second = 0;
			prev_time_in_seconds = time_in_seconds;
			++counter;
		}
	}*/
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	std::cout << "execution_time = " << std::fixed << std::setprecision(1) << duration.count() / 1000000.000 << std::endl;

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

	std::cin.get();
	return 0;
}