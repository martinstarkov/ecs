cmake_minimum_required(VERSION 3.20)

project(ecs_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(ecs_tests "${CMAKE_SOURCE_DIR}/main.cpp" "${CMAKE_SOURCE_DIR}/test_ecs.h" "${CMAKE_SOURCE_DIR}/../include/ecs/ecs.h")

target_include_directories(ecs_tests PRIVATE "${CMAKE_SOURCE_DIR}/../include")