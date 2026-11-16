cmake_minimum_required(VERSION 3.21)
project(NoteCollisionNetworkTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(note_collision_network_tests
    NetworkTests.cpp
    ../../src/network/ProtocolCodec.cpp
    ../../src/network/ClockSynchronizer.cpp
    ../../src/network/LanSessionService.cpp
    ../../src/core/ScoreService.cpp
)
target_include_directories(note_collision_network_tests PRIVATE ../../src)
target_link_libraries(note_collision_network_tests PRIVATE Qt6::Core Qt6::Network)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(note_collision_network_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(note_collision_network_review_tests
    NetworkReviewTests.cpp
    ../../src/network/ProtocolCodec.cpp
    ../../src/network/ClockSynchronizer.cpp
    ../../src/network/LanSessionService.cpp
    ../../src/core/ScoreService.cpp
)
target_include_directories(note_collision_network_review_tests PRIVATE ../../src)
target_link_libraries(note_collision_network_review_tests PRIVATE Qt6::Core Qt6::Network)
