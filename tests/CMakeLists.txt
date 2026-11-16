add_executable(core_tests core/CoreTests.cpp ../src/core/GameEngine.cpp ../src/core/ScoreService.cpp)
target_include_directories(core_tests PRIVATE ../src)
target_link_libraries(core_tests PRIVATE Qt6::Core)
add_test(NAME core_tests COMMAND core_tests)

add_subdirectory(storage)

foreach(test_name IN ITEMS NetworkTests NetworkReviewTests)
    add_executable(${test_name} network/${test_name}.cpp
        ../src/network/ProtocolCodec.cpp ../src/network/ClockSynchronizer.cpp
        ../src/network/LanSessionService.cpp ../src/core/ScoreService.cpp)
    target_include_directories(${test_name} PRIVATE ../src)
    target_link_libraries(${test_name} PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

file(GLOB_RECURSE ui_support_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.h")
list(FILTER ui_support_sources EXCLUDE REGEX "/main\\.cpp$")
add_library(ui_test_support STATIC ${ui_support_sources})
target_include_directories(ui_test_support PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(ui_test_support PUBLIC Qt6::Widgets Qt6::Multimedia Qt6::Network)
foreach(ui_test IN ITEMS UiSmokeTests UiLanTests)
    add_executable(${ui_test} ui/${ui_test}.cpp)
    target_link_libraries(${ui_test} PRIVATE ui_test_support)
    add_test(NAME ${ui_test} COMMAND ${ui_test} "${PROJECT_SOURCE_DIR}/build/evidence")
    set_tests_properties(${ui_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
endforeach()

