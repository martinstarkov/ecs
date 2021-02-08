
#include "ManagerConstructors.h"

#include "OldTests.h"

int main() {

	test3();
	test4();
	test5();
	test6();
	test7();
	test8();
	test9();
	test10();

	LOG("Old tests passed");

	ManagerConstructors();

	LOG("All tests passed");

	std::cin.get();

	return 0;
}