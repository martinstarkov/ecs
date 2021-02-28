
#include "ManagerBasics.h"
#include "EntityBasics.h"
#include "ComponentBasics.h"

#include "OldTests.h"
#include "SpeedTests.h"

int main() {

	ManagerBasics managerBasics;
	EntityBasics entityBasics;
	ComponentBasics componentBasics;
	SpeedTests speedTests;

	test3();
	test4();
	test5();
	test6();
	test7();
	test8();
	test9();
	test10();

	LOG("All tests passed");

	std::cin.get();

	return 0;
}