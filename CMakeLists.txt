cmake_minimum_required(VERSION 3.24)

project(NoteCollision VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Multimedia Network)

file(GLOB_RECURSE NOTE_COLLISION_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

qt_add_executable(NoteCollision ${NOTE_COLLISION_SOURCES})
target_include_directories(NoteCollision PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(NoteCollision PRIVATE Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::Network)

if (MSVC)
    target_compile_options(NoteCollision PRIVATE /W4 /permissive-)
else()
    target_compile_options(NoteCollision PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if (BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

install(TARGETS NoteCollision RUNTIME DESTINATION bin)
install(DIRECTORY assets DESTINATION .)

