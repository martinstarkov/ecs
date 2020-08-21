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

void assign(ecs::Manager3& manager, ecs::EntityId entities, double x = 0, double y = 0) {
	/*manager.ReserveComponent<bool>(1);
	manager.ReserveComponent<Position>(entities);
	manager.ReserveComponent<Velocity>(entities);*/
	manager.ResizeEntities(entities);
	for (ecs::EntityId i = 0; i < entities; ++i) {
		ecs::EntityId entity_id = manager.CreateEntity();
		manager.AddComponent<Position>(entity_id, x, y);
		manager.AddComponent<Velocity>(entity_id, x, y);
		manager.AddComponent<Velocity2>(entity_id, x, y);
		manager.AddComponent<Position2>(entity_id, x, y);
		manager.AddComponent<Velocity3>(entity_id, x, y);
		manager.AddComponent<Position3>(entity_id, x, y);
		// 32 ^
		manager.AddComponent<int>(entity_id, 1);
		manager.AddComponent<double>(entity_id, 2.0);
		manager.AddComponent<float>(entity_id, 3.0f);
		// 16 ^

		// 48
		manager.RemoveComponent<int>(entity_id);
		manager.RemoveComponent<double>(entity_id);
		manager.RemoveComponent<float>(entity_id);
	}
}

void assign2(ecs::Manager3& manager, ecs::EntityId entities, double x = 0, double y = 0) {
	/*manager.ReserveComponent<Velocity>(entities);
	manager.ReserveComponent<Position>(entities);
	manager.ReserveComponent<bool>(1);*/
	for (ecs::EntityId i = 0; i < manager.EntityCount(); ++i) {
		manager.AddComponent<Velocity>(i, x, y);
		manager.AddComponent<Position>(i, x, y);
		manager.AddComponent<int>(i, 1);
		manager.AddComponent<double>(i, 2.0);
		manager.AddComponent<float>(i, 3.0f);
		manager.RemoveComponent<int>(i);
		manager.RemoveComponent<double>(i);
		manager.RemoveComponent<float>(i);
	}
}

void update(ecs::Manager3& manager, int increment = 1) {
	//auto [p, v] = manager.GetComponentVectors<Position, Velocity>();
	for (ecs::EntityId i = 0; i < manager.EntityCount(); ++i) {
		auto& pos = manager.GetComponent<Position>(i);
		pos.x += increment;
		pos.y += increment;
		auto& vel = manager.GetComponent<Velocity>(i);
		vel.x += increment;
		vel.y += increment;
		auto& vel2 = manager.GetComponent<Velocity2>(i);
		vel2.x += increment;
		vel2.y += increment;
		auto& pos2 = manager.GetComponent<Position2>(i);
		pos2.x += increment;
		pos2.y += increment;
		auto& vel3 = manager.GetComponent<Velocity3>(i);
		vel3.x += increment;
		vel3.y += increment;
		auto& pos3 = manager.GetComponent<Position3>(i);
		pos3.x += increment;
		pos3.y += increment;
		/*if (i == 0) {
			LOG("pos: " << pos << ", vel:" << vel << ", int: " << integer << ", double: " << doubler << ", float: " << floater);
		}*/
		if (manager.HasComponent<int>(i)) {
			auto& integer = manager.GetComponent<int>(i);
			integer += increment;
		}
		if (manager.HasComponent<double>(i)) {
			auto& doubler = manager.GetComponent<double>(i);
			doubler += increment;
		}
		if (manager.HasComponent<float>(i)) {
			auto& floater = manager.GetComponent<float>(i);
			floater += increment;
		}
	}
}

int fpsLimit() { return 240; }

int main() {
	ecs::Manager3 manager;
	//ecs::Manager3 manager2;
	ecs::EntityId entities = 100000;
	std::size_t loops = 1000;
	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	assign(manager, entities, 0, 0);
	//assign2(manager2, entities, 100, 100);
	LOG("ASSIGNEMT COMPLETED!");
	LOG("TIMING LOOPS!");
	auto start = std::chrono::high_resolution_clock::now();
	for (std::size_t i = 0; i < loops; ++i) {
		update(manager, 0);
		//LOG(i);
	}
	LOG("LOOPS COMPLETED!");
	//using namespace std::chrono;
	//using dsec = duration<double>;
	//auto invFpsLimit = duration_cast<system_clock::duration>(dsec{ 1. / fpsLimit() });
	//auto m_BeginFrame = system_clock::now();
	//auto m_EndFrame = m_BeginFrame + invFpsLimit;
	//unsigned frame_count_per_second = 0;
	//auto prev_time_in_seconds = time_point_cast<seconds>(m_BeginFrame);
	//size_t counter = 0;
	//while (counter < 10) {
	//	// Do drawing work ...
	//	if (counter == 4) {
	//		//assign2(manager, entities, 0, 0);
	//	}
	//	update(manager);
	//	//update(manager2);
	//	// This part is just measuring if we're keeping the frame rate.
	//	// It is not necessary to keep the frame rate.
	//	auto time_in_seconds = time_point_cast<seconds>(system_clock::now());
	//	++frame_count_per_second;
	//	if (time_in_seconds > prev_time_in_seconds) {
	//		//LOG("manager1 pos: " << manager.GetComponent<Position>(0));
	//		//LOG("manager2 pos: " << manager2.GetComponent<Position>(0));
	//		std::cerr << frame_count_per_second << " frames per second\n";
	//		//LOG("pos1: " << manager.GetComponentStorage().GetComponentId<Position>() << ", vel1: " << manager.GetComponentStorage().GetComponentId<Velocity>() << ", bool1: " << manager.GetComponentStorage().GetComponentId<bool>());
	//		//LOG("pos2: " << manager2.GetComponentStorage().GetComponentId<Position>() << ", vel2: " << manager2.GetComponentStorage().GetComponentId<Velocity>() << ", bool2: " << manager2.GetComponentStorage().GetComponentId<bool>());
	//		frame_count_per_second = 0;
	//		prev_time_in_seconds = time_in_seconds;
	//		++counter;
	//	}

	//	// This part keeps the frame rate.
	//	//std::this_thread::sleep_until(m_EndFrame);
	//	m_BeginFrame = m_EndFrame;
	//	m_EndFrame = m_BeginFrame + invFpsLimit;
	//}
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	// To get the value of duration use the count() 
	// member function on the duration object 
	std::cout << "execution_time = "
		<< std::fixed << std::setprecision(3)
		<< duration.count() / 1000000.000 << std::endl;
	//LOG("Manager size/capacity: " << manager.Size() << "/" << manager.Capacity());
	//manager.~Manager3();

	// Model 3 - 2 get, 3 has

	// 100k, 1k loops, 21mb, vector lookup, 15.5
	// 100k, 1k loops, 21mb, vector lookup, 14.8
	// 100k, 1k loops, 21mb, vector lookup, 14.7
	// 100k, 1k loops, 21mb, vector lookup, 14.7

	// 100k, 1k loops, 4mb, map lookup, 15.8
	// 100k, 1k loops, 4mb, map lookup, 15.6
	// 100k, 1k loops, 4mb, map lookup, 15.6
	// 100k, 1k loops, 4mb, map lookup, 15.3

	// 100k, 1k loops, 43.2mb, unordered_map lookup, 28.3
	// 100k, 1k loops, 43.2mb, unordered_map lookup, 28.7
	// 100k, 1k loops, 43.2mb, unordered_map lookup, 28.1
	// 100k, 1k loops, 43.2mb, unordered_map lookup, 28.6

	// 10k, 10k loops, 3mb, vector lookup, 7.8
	// 10k, 10k loops, 3mb, vector lookup, 7.5
	// 10k, 10k loops, 3mb, vector lookup, 8.4
	// 10k, 10k loops, 3mb, vector lookup, 7.7

	// 10k, 10k loops, 4mb, map lookup, 10.9
	// 10k, 10k loops, 4mb, map lookup, 12.4
	// 10k, 10k loops, 4mb, map lookup, 10.1
	// 10k, 10k loops, 4mb, map lookup, 13.6

	// Model 3 - 6 get, 3 has

	// 100k, 1k loops, mb, vector lookup, 28.2
	// 100k, 1k loops, mb, vector lookup, 
	// 100k, 1k loops, mb, vector lookup, 
	// 100k, 1k loops, mb, vector lookup, 

	// 1k, 50k loops: 4 = X, 3 = 3.7, 3.9, 9.9
	// 1k, 50k loops: 4 = 3.5, 3 = 3.5
	// 100k, 5k loops: 4 = 61.2, 3 = 51.5
	// 1 mil, 500 loops: 4 = 59, 3 = 51.2
	// 10 mil, 100 loops: 4 = 116.7, 3 = 102.2
	std::cin.get();
	return 0;
}