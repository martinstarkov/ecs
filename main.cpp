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

void assign(ecs::Manager6& manager, ecs::EntityId entities) {
	for (ecs::EntityId counter = 0; counter < entities; ++counter) {
		ecs::EntityId i = manager.CreateEntity();
		manager.AddComponent<Test<1>>(i, 0, 1);
		manager.AddComponent<Test<2>>(i, 0, 1);
		manager.AddComponent<Test<3>>(i, 0, 1);
		manager.AddComponent<Test<4>>(i, 0, 1);
		manager.AddComponent<Test<5>>(i, 0, 1);
		manager.AddComponent<Test<6>>(i, 0, 1);
		manager.AddComponent<Test<7>>(i, 0, 1);
		manager.AddComponent<Test<8>>(i, 0, 1);
		manager.AddComponent<Test<9>>(i, 0, 1);
		manager.AddComponent<Test<10>>(i, 1, 0);
		manager.AddComponent<Test<11>>(i, 1, 0);
		manager.AddComponent<Test<12>>(i, 1, 0);
		manager.AddComponent<Test<13>>(i, 1, 0);
		manager.AddComponent<Test<14>>(i, 1, 0);
		manager.AddComponent<Test<15>>(i, 1, 0);
		manager.AddComponent<Test<16>>(i, 1, 0);
		manager.AddComponent<Test<17>>(i, 1, 0);
		manager.AddComponent<Test<18>>(i, 1, 0);
		manager.AddComponent<Test<19>>(i, 1, 0);
		manager.AddComponent<Test<20>>(i, 1, 0);
	}
}

void update(ecs::Manager6& manager) {
	for (ecs::EntityId i = ecs::first_valid_entity; i < manager.EntityCount(); ++i) {
		manager.GetComponent<Test<1>>(i);
		manager.GetComponent<Test<2>>(i);
		manager.GetComponent<Test<3>>(i);
		manager.GetComponent<Test<4>>(i);
		manager.GetComponent<Test<5>>(i);
		manager.GetComponent<Test<6>>(i);
		manager.GetComponent<Test<7>>(i);
		manager.GetComponent<Test<8>>(i);
		manager.GetComponent<Test<9>>(i);
		manager.GetComponent<Test<10>>(i);
		manager.GetComponent<Test<11>>(i);
		manager.GetComponent<Test<12>>(i);
		manager.GetComponent<Test<13>>(i);
		manager.GetComponent<Test<14>>(i);
		manager.GetComponent<Test<15>>(i);
		manager.GetComponent<Test<16>>(i);
		manager.GetComponent<Test<17>>(i);
		manager.GetComponent<Test<18>>(i);
		manager.GetComponent<Test<19>>(i);
		manager.GetComponent<Test<20>>(i);
	}
}

int main() {
	ecs::EntityId entities = 1000000;
	ecs::Manager6 manager(entities, 20);
	std::size_t loops = 1000;
	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	assign(manager, entities);
	LOG("ASSIGNEMT COMPLETED!");
	LOG("TIMING LOOPS!");
	auto start = std::chrono::high_resolution_clock::now();
	for (std::size_t i = 0; i < loops; ++i) {
		update(manager);
	}
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