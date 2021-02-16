#pragma once

#include "../ECS_new.h"

#include "Timer.h"

#include <type_traits>
#include <cassert>
#include <utility>
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>


#define LOG(x) { std::cout << x << std::endl; }
#define LOG_(x) { std::cout << x; }