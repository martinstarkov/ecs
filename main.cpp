#include "ECS.h"

struct Position {
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Position& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

struct Velocity {
	double x = 0.0, y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Velocity& obj) {
		os << "(" << obj.x << "," << obj.y << ")";
		return os;
	}
};

int main() {
	ecs::Manager manager;
	size_t size = 100;
	for (size_t i = 0; i < size; ++i) {
		ecs::Entity entity(manager.CreateEntity(), manager);
		entity.AddComponent<Position>(1.0, 1.0);
		Position pos;
		if (entity.HasComponent<Position>()) {
			auto& pos = entity.GetComponent<Position>();
			pos.x += static_cast<double>(rand() % 20 - 10);
			pos.y += static_cast<double>(rand() % 20 - 10);
		}
		if (i > size / 2) {
			entity.AddComponent<Velocity>(1.0, 1.0);
			if (entity.HasComponent<Velocity>()) {
				auto& vel = entity.GetComponent<Velocity>();
				vel.x += static_cast<double>(rand() % 20 - 10);
				vel.y += static_cast<double>(rand() % 20 - 10);
			}
			if (entity.HasComponent<Position>()) {
				entity.RemoveComponent<Position>();
			}
		}
		//LOG(i << ": pos: " << entity.HasComponent<Position>() << ", vel: " << entity.HasComponent<Velocity>());
	}
	//manager.GetComponentStorage().printComponents<Position, Velocity>();
	/*LOG("Complete: " << sizeof(manager));
	std::cin.get();*/
	return 0;
}