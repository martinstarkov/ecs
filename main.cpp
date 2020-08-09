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
	for (size_t i = 0; i < 200; ++i) {
		LOG(i);
		ecs::Entity entity(manager.CreateEntity(), manager);
		entity.AddComponent<Position>(1.0, 1.0);
		if (i > 100) {
			entity.AddComponent<Velocity>(1.0, 1.0);
			if (entity.HasComponent<Velocity>()) {
				auto& vel = entity.GetComponent<Velocity>();
				vel.x += static_cast<double>(rand() % 20 - 10);
				vel.y += static_cast<double>(rand() % 20 - 10);
			}
		}
		if (entity.HasComponent<Position>()) {
			auto& pos = entity.GetComponent<Position>();
			pos.x += static_cast<double>(rand() % 20 - 10);
			pos.y += static_cast<double>(rand() % 20 - 10);
		}
	}
	LOG("Complete!");
	std::cin.get();
	return 0;
}