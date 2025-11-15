#pragma once

#include <iostream>

template <typename... Args>
void Print(Args&&... args) {
	(std::cout << ... << args) << '\n';
}