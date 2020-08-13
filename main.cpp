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
    ecs::ComponentStorage c;
    c.push_back('a');
    c.push_back(1);
    c.push_back(2.0);
    c.push_back(3);
    c.push_back(Position{ 3, 3 });
    c.push_back(Position{ 4, 4 });
    c.push_back(Velocity{ 5, 5 });
	c.push_back(Velocity{ 6, 6 });
    std::cout << "Total size of c: " << c.TotalSize() << std::endl;
    std::cout << "different data types in c: " << c.Size() << std::endl;
    std::cout << "Number of integers in c: " << c.number_of<int>() << std::endl;
    std::cout << "Number of strings in c: " << c.number_of<std::string>() << std::endl;
    auto vectors = c.getComponentVectors<int, Position>();
	auto& ints = ecs::get<int>(vectors);
    auto& strings = ecs::get<Position>(vectors);
    LOG_("Ints: ");
    for (auto& c : ints) {
        LOG_(c << ", ");
    }
    LOG("");
    LOG_("Strings: ");
    for (auto& c : strings) {
        LOG_(c << ", ");
    }
    LOG("");
    auto [pos, vel] = c.getComponents<Position, Velocity>(1);
    LOG("Entity 0 has components : " << pos << "," << vel);
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
	/*LOG("Complete: " << sizeof(manager));
	std::cin.get();*/
	return 0;
}