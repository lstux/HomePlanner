cmake_minimum_required(VERSION 3.16)
project(home_sync_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(home_sync_core
  src/sync_core.cpp
  src/sync_journal.cpp
)
target_include_directories(home_sync_core PUBLIC include)

add_executable(sync_core_tests tests/sync_core_tests.cpp)
target_link_libraries(sync_core_tests PRIVATE home_sync_core)

add_executable(sync_journal_tests tests/sync_journal_tests.cpp)
target_link_libraries(sync_journal_tests PRIVATE home_sync_core)

enable_testing()
add_test(NAME sync_core_tests COMMAND sync_core_tests)
add_test(NAME sync_journal_tests COMMAND sync_journal_tests)
