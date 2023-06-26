#pragma once

#include <iostream>

//#include "test_ecs.h"
//#include "ecs/ecs.h"
#include "ecs/updated_ecs.h"

#define LOG(x) { std::cout << x << std::endl; }

struct Position {
	Position() {
		LOG("Default construction");
	}
	Position(int x, int y) : x{ x }, y{ y } {
		LOG("Specific construction");
	}
	~Position() {
		LOG("Destruction");
	}
	Position(const Position& copy) : x{ copy.x }, y{ copy.y } {
		LOG("Copy");
	}
	Position& operator=(const Position& copy) {
		LOG("Copy assignment");
		this->x = copy.x;
		this->y = copy.y;
		return *this;
	}
	Position(Position&& move) noexcept : x{ move.x }, y{ move.y } {
		LOG("Move");
		move.x = -1;
		move.y = -1;
	}
	Position& operator=(Position&& move) noexcept {
		LOG("Move assignment");
		this->x = move.x;
		this->y = move.y;
		move.x = -1;
		move.y = -1;
		return *this;
	}
	int x = 6;
	int y = 9;
};


int main(int argc, char* argv[]) {
	//TestECS();
	ecs::impl::Pool<Position> pool;
	pool.Add(20, 3, 2);
	LOG(pool.Get(20).x);
	LOG(pool.Get(20).y);
	pool.Add(21, 6, 5);
	LOG(pool.Get(20).x);
	LOG(pool.Get(20).y);
	LOG(pool.Get(21).x);
	LOG(pool.Get(21).y);
	pool.Add(19, 8, 7);
	LOG(pool.Get(20).x);
	LOG(pool.Get(20).y);
	LOG(pool.Get(21).x);
	LOG(pool.Get(21).y);
	LOG(pool.Get(19).x);
	LOG(pool.Get(19).y);
	pool.Add(19, 8, 8);
	LOG(pool.Get(19).x);
	LOG(pool.Get(19).y);
	pool.Remove(19);
	pool.Add(40, 3, 1);
	pool.Add(19, 9, 9);
	LOG(pool.Get(19).x);
	LOG(pool.Get(19).y);
	LOG(pool.Get(40).x);
	LOG(pool.Get(40).y);
	pool.Remove(40);
	pool.Remove(19);
	pool.Add(19, 55, 55);
	pool.Add(40, 66, 66);
	LOG(pool.Get(19).x);
	LOG(pool.Get(19).y);
	LOG(pool.Get(40).x);
	LOG(pool.Get(40).y);
	//TestECS();
	return 0;
}