#include "ECS.h"

#include <time.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>

#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }

template <std::size_t I>
struct TPos { // 128 bytes total
	TPos(std::int64_t a = 0) : a{ a } {}
	friend std::ostream& operator<<(std::ostream& os, const TPos& obj) {
		os << "(" << obj.a << "," << obj.b << "," << obj.c << "," << obj.d << ")";
		return os;
	}
	std::int64_t a = 0; // 8 bytes
	std::int64_t b = 0; // 8 bytes
	std::int64_t c = 0; // 8 bytes
	std::int64_t d = 0; // 8 bytes

	std::int64_t e = 0; // 8 bytes
	std::int64_t f = 0; // 8 bytes
	std::int64_t g = 0; // 8 bytes
	std::int64_t h = 0; // 8 bytes
};

void test3() {
	ecs::Manager ecs;
	auto start = std::chrono::high_resolution_clock::now();
	for (auto i = 0; i < 20000; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<1>>(3);
		e.AddComponent<TPos<2>>(3);
		e.AddComponent<TPos<3>>(3);
		e.AddComponent<TPos<4>>(3);
		e.AddComponent<TPos<5>>(3);
		//e.RemoveComponent<TPos<3>>();
	}
	for (auto i = 0; i < 20000; ++i) {
		auto e = ecs.CreateEntity();
		e.AddComponent<TPos<6>>(5);
		e.AddComponent<TPos<7>>(5);
		e.AddComponent<TPos<8>>(5);
		//e.RemoveComponent<TPos<8>>();
		e.AddComponent<TPos<9>>(5);
		e.AddComponent<TPos<10>>(5);
	}
	auto stop_addition = std::chrono::high_resolution_clock::now();
	auto duration_addition = std::chrono::duration_cast<std::chrono::microseconds>(stop_addition - start);
	std::cout << "test3 took " << std::fixed << std::setprecision(1) << duration_addition.count() / 1000000.000 << " seconds to add all components" << std::endl;
	for (auto i = 0; i < 2; ++i) {
		ecs.ForEach<TPos<1>, TPos<5>>([&] (auto& pos, auto& pos2) {
			//LOG_(pos.a << " -> ");
			pos.a += 1;
			//LOG(pos.a);
			pos.d += 1;
			pos2.a += 1;
			pos2.d += 1;
			ecs.ForEach<TPos<6>, TPos<10>>([&](auto& pos3, auto& pos4) {
				pos3.a += 1;
				pos3.d += 1;
				pos4.a += 1;
				pos4.d += 1;
			}, false);
		}, false);
	}
	auto stop = std::chrono::high_resolution_clock::now();
	auto duration_get = std::chrono::duration_cast<std::chrono::microseconds>(stop - stop_addition);
	std::cout << "test3 get component loop time = " << std::fixed << std::setprecision(1) << duration_get.count() / 1000000.000 << std::endl;
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
	std::cout << "test3 total execution_time = " << std::fixed << std::setprecision(1) << duration.count() / 1000000.000 << std::endl;
}

void test9() {
	ecs::Manager ecs;
	auto e1 = ecs.CreateEntity();
	auto e2 = ecs.CreateEntity();
	auto e3 = ecs.CreateEntity();
	auto e4 = ecs.CreateEntity();
	e1.AddComponent<TPos<0>>(69);
	e2.AddComponent<TPos<0>>(70);
	e2.AddComponent<TPos<1>>(71);

	e3.AddComponent<TPos<0>>(72);
	e3.AddComponent<TPos<1>>(73);
	e3.AddComponent<TPos<2>>(74);
	e4.AddComponent<TPos<1>>(75);
	e4.AddComponent<TPos<2>>(76);
	ecs.ForEach<TPos<1>>([](auto& pos_one) {
		LOG_(pos_one.a << " -> ");
		pos_one.a += 1;
		LOG(pos_one.a);
	});
	ecs.ForEach<TPos<0>>([](auto& pos_zero) {
		LOG("Entity TPos<0> after: " << pos_zero.a);
	});
}

int main() {
	test3();
	return 0;
}