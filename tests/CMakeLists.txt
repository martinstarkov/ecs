cmake_minimum_required(VERSION 3.20)

project(ecs_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    main.cpp
    test.h
    hooks.h
    profile.h
    views.h
    common.h
    sparse_dense.h
    ../include/ecs/ecs.h)

add_executable(ecs_tests ${SOURCES})

target_include_directories(ecs_tests
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
