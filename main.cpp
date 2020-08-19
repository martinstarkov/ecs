#include "ECS.h"

#include <time.h>
#include <chrono>
#include <thread>

struct Position {
	Position() {
		//LOG("Created");
	}
	Position(double x, double y) : x(x), y(y) {
		//LOG("Created with specific arguments");
	}
	Position(const Position& other) = default;
	Position(Position&& other) = default;
	Position& operator=(const Position& other) = default;
	Position& operator=(Position&& other) = default;
	~Position() {
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

void assign(ecs::Manager4& manager, ecs::EntityId entities, double x = 0, double y = 0) {
	/*manager.ReserveComponent<bool>(1);
	manager.ReserveComponent<Position>(entities);
	manager.ReserveComponent<Velocity>(entities);*/
	manager.ResizeEntities(entities);
	for (ecs::EntityId i = 0; i < entities; ++i) {
		ecs::EntityId entity_id = manager.CreateEntity();
		manager.AddComponent<Position>(entity_id, x, y);
		manager.AddComponent<Velocity>(entity_id, x, y);
		// 32 ^
		manager.AddComponent<int>(entity_id, 1);
		manager.AddComponent<double>(entity_id, 2.0);
		manager.AddComponent<float>(entity_id, 3.0f);
		// 16 ^

		// 48
	}
}

void assign2(ecs::Manager4& manager, ecs::EntityId entities, double x = 0, double y = 0) {
	/*manager.ReserveComponent<Velocity>(entities);
	manager.ReserveComponent<Position>(entities);
	manager.ReserveComponent<bool>(1);*/
	manager.ResizeEntities(entities);
	for (ecs::EntityId i = 0; i < entities; ++i) {
		ecs::EntityId entity_id = manager.CreateEntity();
		manager.AddComponent<Velocity>(entity_id, x, y);
		manager.AddComponent<Position>(entity_id, x, y);
		manager.AddComponent<int>(entity_id, 1);
		manager.AddComponent<double>(entity_id, 2.0);
		manager.AddComponent<float>(entity_id, 3.0f);
	}
}

void update(ecs::Manager4& manager, int increment = 1) {
	//auto [p, v] = manager.GetComponentVectors<Position, Velocity>();
	for (ecs::EntityId i = 0; i < manager.EntityCount(); ++i) {
		auto& pos = manager.GetComponent<Position>(i);
		pos.x += increment;
		pos.y += increment;
		auto& vel = manager.GetComponent<Velocity>(i);
		vel.x += increment;
		vel.y += increment;
		auto& integer = manager.GetComponent<int>(i);
		integer += increment;
		auto& doubler = manager.GetComponent<double>(i);
		doubler += increment;
		auto& floater = manager.GetComponent<float>(i);
		floater += increment;
		/*if (i == 0) {
			LOG("pos: " << pos << ", vel:" << vel << ", int: " << integer << ", double: " << doubler << ", float: " << floater);
		}*/
	}
}

int fpsLimit() { return 240; }

int main() {
	ecs::Manager4 manager;
	//ecs::Manager4 manager2;
	ecs::EntityId entities = 80000;
	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	assign(manager, entities, 0, 0);
	//assign2(manager2, entities, 100, 100);
	LOG("ASSIGNEMT COMPLETED!");
	using namespace std::chrono;
	using dsec = duration<double>;
	auto invFpsLimit = duration_cast<system_clock::duration>(dsec{ 1. / fpsLimit() });
	auto m_BeginFrame = system_clock::now();
	auto m_EndFrame = m_BeginFrame + invFpsLimit;
	unsigned frame_count_per_second = 0;
	auto prev_time_in_seconds = time_point_cast<seconds>(m_BeginFrame);
	size_t counter = 0;
	while (counter < 5) {
		// Do drawing work ...
		update(manager);
		//update(manager2);
		// This part is just measuring if we're keeping the frame rate.
		// It is not necessary to keep the frame rate.
		auto time_in_seconds = time_point_cast<seconds>(system_clock::now());
		++frame_count_per_second;
		if (time_in_seconds > prev_time_in_seconds) {
			//LOG("manager1 pos: " << manager.GetComponent<Position>(0));
			//LOG("manager2 pos: " << manager2.GetComponent<Position>(0));
			std::cerr << frame_count_per_second << " frames per second\n";
			//LOG("pos1: " << manager.GetComponentStorage().GetComponentId<Position>() << ", vel1: " << manager.GetComponentStorage().GetComponentId<Velocity>() << ", bool1: " << manager.GetComponentStorage().GetComponentId<bool>());
			//LOG("pos2: " << manager2.GetComponentStorage().GetComponentId<Position>() << ", vel2: " << manager2.GetComponentStorage().GetComponentId<Velocity>() << ", bool2: " << manager2.GetComponentStorage().GetComponentId<bool>());
			frame_count_per_second = 0;
			prev_time_in_seconds = time_in_seconds;
			++counter;
		}

		// This part keeps the frame rate.
		std::this_thread::sleep_until(m_EndFrame);
		m_BeginFrame = m_EndFrame;
		m_EndFrame = m_BeginFrame + invFpsLimit;
	}
	//LOG("Manager size/capacity: " << manager.Size() << "/" << manager.Capacity());
	//manager.~Manager4();
	std::cin.get();
	return 0;
}