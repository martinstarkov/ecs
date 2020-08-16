#include "ECS.h"

struct Position {
	Position() {
		//LOG("Created");
	}
	Position(int x, int y) : x(x), y(y) {
		//LOG("Created with specific arguments");
	}
	Position(const Position& other) {
		LOG("Copied");
		*this = other;
	}
	Position(Position&& other) {
		LOG("Moved");
		*this = other;
	}
	Position& operator=(const Position& other) = default;
	Position& operator=(Position&& other) = default;
	~Position() {
		//LOG("Destroyed");
	}
	int x = 0, y = 0;
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

int main() {
	ecs::Manager manager;
	ecs::EntityId entities = 3000;
	std::cin.get();
	LOG("ASSIGNING POSITIONS AND VELOCITIES TO " << entities << " ENTITIES...");
	manager.ReserveComponent<Position>(entities);
	manager.ReserveComponent<Velocity>(entities);
	manager.ResizeEntities(entities);
	for (ecs::EntityId i = 0; i < entities; ++i) {
		ecs::EntityId entity_id = manager.CreateEntity();
		manager.AddComponent<Position>(entity_id, 3, 3);
		manager.AddComponent<Velocity>(entity_id, 5, 5);
	}
	LOG("ASSIGNEMT COMPLETED!");
	std::cin.get();
	LOG("LOOPING OVER POSITIONS AND VELOCITIES AND INCREMENTING BY 20 (" << entities << ")");
	auto [p, v] = manager.GetComponentVectors<Position, Velocity>();
	for (ecs::EntityId i = 0; i < manager.EntityCount(); ++i) {
		auto& pos = manager.GetComponent<Position>(p, i);
		pos.x += 20;
		pos.y += 20;
		auto& vel = manager.GetComponent<Velocity>(v, i);
		vel.x += 20;
		vel.y += 20;
	}
	LOG("COMPLETED LOOPING!");
	std::cin.get();
    LOG("POSITIONS AND VELOCITIES: ");
	manager.PrintComponents<Position, Velocity>();
	/*std::cout << "Total size of c: " << c.TotalSize() << std::endl;
	std::cout << "different data types in c: " << c.UniqueSize() << std::endl;
	std::cout << "Number of positions in c: " << c.Count<Position>() << std::endl;*/


    //auto [pos, vel] = c.GetComponents<Position, Velocity>(0);
    //LOG("Entity 0 has components : " << pos << "," << vel);
	//ecs::Manager manager;
	//size_t size = 100;
	//for (size_t i = 0; i < size; ++i) {
	//	ecs::Entity entity(manager.CreateEntity(), manager);
	//	entity.AddComponent<Position>(1.0, 1.0);
	//	Position pos;
	//	if (entity.HasComponent<Position>()) {
	//		auto& pos = entity.GetComponent<Position>();
	//		pos.x += static_cast<double>(rand() % 20 - 10);
	//		pos.y += static_cast<double>(rand() % 20 - 10);
	//	}
	//	if (i > size / 2) {
	//		entity.AddComponent<Velocity>(1.0, 1.0);
	//		if (entity.HasComponent<Velocity>()) {
	//			auto& vel = entity.GetComponent<Velocity>();
	//			vel.x += static_cast<double>(rand() % 20 - 10);
	//			vel.y += static_cast<double>(rand() % 20 - 10);
	//		}
	//		if (entity.HasComponent<Position>()) {
	//			entity.RemoveComponent<Position>();
	//		}
	//	}
	//	//LOG(i << ": pos: " << entity.HasComponent<Position>() << ", vel: " << entity.HasComponent<Velocity>());
	//}
	//manager.GetComponentStorage().printComponents<Position, Velocity>();
	/*LOG("Complete: " << sizeof(manager));*/
	std::cin.get();
	return 0;
}