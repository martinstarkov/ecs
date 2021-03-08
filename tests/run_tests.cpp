
#include "ManagerBasics.h"
#include "EntityBasics.h"
#include "ComponentBasics.h"
#include "PoolTests.h"

#include "OldTests.h"
#include "SpeedTests.h"

#include <iostream>

int main() {

	ManagerBasics();
	EntityBasics();
	ComponentBasics();
	SpeedTests();
	PoolTests();

	test3();
	test4();
	test5();
	test6();
	test7();
	test8();
	test9();
	test10();

	std::cout << "All tests passed" << std::endl;

	std::cin.get();

	return 0;
}