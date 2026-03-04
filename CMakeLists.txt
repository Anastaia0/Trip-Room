cmake_minimum_required(VERSION 3.21)
project(TripPlannerServerLogic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

add_library(trip_logic
    src/trip_service_core.cpp
    src/trip_service_itinerary.cpp
    src/trip_service_tasks.cpp
    src/trip_service_budget.cpp
    src/trip_service_collab.cpp
    src/trip_service_io.cpp
)

target_include_directories(trip_logic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(trip_logic_tests
    tests/trip_service_tests.cpp
)

target_link_libraries(trip_logic_tests
    PRIVATE
        trip_logic
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(trip_logic_tests)