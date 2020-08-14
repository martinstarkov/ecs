#include "ECS.h"

struct Position {
	Position() {
		//LOG("Created");
	}
	Position(double x, double y) : x(x), y(y) {
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
	double x = 0.0, y = 0.0;
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
    ecs::ComponentStorage c;

	size_t size = 1000000; // 1 mil

	c.Reserve<Position>(size);
	c.Reserve<Velocity>(size);

	std::cin.get();

	LOG("ASSIGNING POSITIONS TO (1, 1) AND VELOCITIES TO (2, 2) (size: " << size << ")...");
	for (size_t i = 0; i < size; ++i) {
		c.EmplaceBack<Position>(1, 1);
		c.EmplaceBack<Velocity>(2, 2);
	}
	LOG("POSITION AND VELOCITY ASSIGNMENT COMPLETE!");

	std::cin.get();

	LOG("ADDING 20 TO EACH POSITION AND VELOCITY (" << size << ")...");
	auto [p, v] = c.GetComponentVectors<Position, Velocity>();
	for (size_t i = 0; i < size; ++i) {
		auto& pos = p[i];
		pos.x += 20;
		pos.y += 20;
		auto& vel = v[i];
		vel.x += 20;
		vel.y += 20;
	}
	LOG("ADDITION COMPLETE!");

	std::cin.get();

    LOG_("POSITIONS AND VELOCITIES: ");
	auto [pos, vel] = c.GetComponentVectors<Position, Velocity>();
	for (size_t i = 0; i < size; ++i) {
        LOG_(pos[i] << ", " << vel[i]);
    }
    LOG("");
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