cmake_minimum_required(VERSION 3.21)
project(NoteCollisionCoreTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(note_collision_core_tests
    CoreTests.cpp
    ../../src/core/GameEngine.cpp
    ../../src/core/ScoreService.cpp
)
target_include_directories(note_collision_core_tests PRIVATE ../../src)
target_link_libraries(note_collision_core_tests PRIVATE Qt6::Core)
