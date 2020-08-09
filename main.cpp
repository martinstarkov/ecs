#include "ECS.h"

#include <iostream>

#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

struct Position {
	double x = 0.0;
	double y = 0.0;
	friend std::ostream& operator<<(std::ostream& os, const Position& obj) {
		os << obj.x << "," << obj.y;
		return os;
	}
};

int main() {
	ecs::Manager manager;
	ecs::Entity entity(manager.CreateIndex(), manager);
	auto& pos = entity.AddComponent<Position>(3.0, 4.0);
	LOG(pos);
	pos.x += 5;
	pos.y += 5;
	auto& posAfter = entity.GetComponent<Position>();
	LOG(posAfter);
	posAfter.x -= 5;
	posAfter.y -= 5;
	auto& posLast = entity.GetComponent<Position>();
	LOG(posLast);
	LOG(entity.HasComponent<Position>());
	std::cin.get();
	return 0;
}