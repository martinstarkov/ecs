#include "ECS.h"

#include <time.h>
#include <chrono>
#include <thread>
#include <iomanip>

struct Position {
	Position() {
		//LOG("Created");
	}
	Position(double x, double y) : x(x), y(y) {
		//LOG("Created with specific arguments");
	}
	Position(const Position&) = default;
	Position& operator=(const Position&) = default;
	Position(Position&&) = default;
	Position& operator=(Position&&) = default;
	~Position() noexcept {
		//LOG("Destroyed");
	}
	double x = 0, y = 0;
	friend std::ostream& operator<<(std::ostream& os, const Position& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Velocity {
	Velocity(double x, double y) : x(x), y(y) {}
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Velocity& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Velocity2 {
	Velocity2(double x, double y) : x(x), y(y) {}
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Velocity2& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Position2 {
	Position2(double x, double y) : x(x), y(y) {}
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Position2& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Velocity3 {
	Velocity3(double x, double y) : x(x), y(y) {}
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Velocity3& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Position3 {
	Position3(double x, double y) : x(x), y(y) {}
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Position3& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

void assign(ecs::Manager5& manager, ecs::EntityId entities, double x = 0, double y = 0) {
	/*manager.ReserveComponent<bool>(1);
	manager.ReserveComponent<Position>(entities);
	manager.ReserveComponent<Velocity>(entities);*/
	manager.ResizeEntities(entities);
	for (ecs::EntityId i = 0; i < entities; ++i) {
		ecs::EntityId entity_id = manager.CreateEntity();
		manager.AddComponent<Position>(entity_id, 0, 0);
		manager.AddComponent<Velocity>(entity_id, 0, 0);
		// 32 ^
		manager.AddComponent<Velocity2>(entity_id, 0, 0);
		manager.AddComponent<Position2>(entity_id, 0, 0);
		// 32 ^
		manager.AddComponent<Velocity3>(entity_id, 0, 0);
		manager.AddComponent<Position3>(entity_id, 0, 0);
		// 32 ^
		manager.AddComponent<int>(entity_id, 1);
		manager.AddComponent<double>(entity_id, 2.0);
		manager.AddComponent<float>(entity_id, 3.0f);
	}
}

void update(ecs::Manager5& manager, int increment = 1) {
	//auto [p, v] = manager.GetComponentVectors<Position, Velocity>();
	for (std::size_t i = 0; i < manager.EntityCount(); ++i) {
		ecs::EntityId entity_id = i;
		manager.GetComponent<Position>(entity_id);
		manager.GetComponent<Velocity>(entity_id);
		// 32 ^
		manager.GetComponent<Velocity2>(entity_id);
		manager.GetComponent<Position2>(entity_id);
		// 32 ^
		manager.GetComponent<Velocity3>(entity_id);
		manager.GetComponent<Position3>(entity_id);
		// 32 ^
		manager.GetComponent<int>(entity_id);
		manager.GetComponent<double>(entity_id);
		manager.GetComponent<float>(entity_id);
	}
}

int fpsLimit() { return 240; }

int main() {
	ecs::Manager5 manager;
	//ecs::Manager5 manager2;
	ecs::EntityId entities = 10000;
	std::size_t loops = 50;
	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	assign(manager, entities, 0, 0);
	LOG("ASSIGNEMT COMPLETED!");
	LOG("TIMING LOOPS!");
	LOG("LOOPS COMPLETED!");
	using namespace std::chrono;
	using dsec = duration<double>;
	auto invFpsLimit = duration_cast<system_clock::duration>(dsec{ 1. / fpsLimit() });
	auto m_BeginFrame = system_clock::now();
	auto m_EndFrame = m_BeginFrame + invFpsLimit;
	unsigned frame_count_per_second = 0;
	auto prev_time_in_seconds = time_point_cast<seconds>(m_BeginFrame);
	size_t counter = 0;
	auto start = std::chrono::high_resolution_clock::now();
	while (counter <= 10) {
		for (std::size_t i = 0; i < loops; ++i) {
			update(manager, 0);
		}
		// This part is just measuring if we're keeping the frame rate.
		// It is not necessary to keep the frame rate.
		auto time_in_seconds = time_point_cast<seconds>(system_clock::now());
		++frame_count_per_second;
		if (time_in_seconds > prev_time_in_seconds) {
			std::cerr << frame_count_per_second << " frames per second\n";
			frame_count_per_second = 0;
			prev_time_in_seconds = time_in_seconds;
			++counter;
		}

		// This part keeps the frame rate.
		//std::this_thread::sleep_until(m_EndFrame);
		m_BeginFrame = m_EndFrame;
		m_EndFrame = m_BeginFrame + invFpsLimit;
	}
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	// To get the value of duration use the count() 
	// member function on the duration object 
	std::cout << "execution_time = "
		<< std::fixed << std::setprecision(3)
		<< duration.count() / 1000000.000 << std::endl;
	//LOG("Manager size/capacity: " << manager.Size() << "/" << manager.Capacity());
	//manager.~Manager5();


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