add_executable(storage_tests StorageTests.cpp
    ../../src/storage/SongRepository.cpp
    ../../src/storage/RecordRepository.cpp
    ../../src/storage/SettingsRepository.cpp
    ../../src/core/ScoreService.cpp)
target_include_directories(storage_tests PRIVATE ../../src)
target_link_libraries(storage_tests PRIVATE Qt6::Core)
target_compile_definitions(storage_tests PRIVATE
    NOTE_COLLISION_SONGS_DIR="${PROJECT_SOURCE_DIR}/assets/songs")
add_test(NAME storage_tests COMMAND storage_tests)
