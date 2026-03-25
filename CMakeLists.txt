cmake_minimum_required(VERSION 3.21)
project(TripPlannerServerLogic LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

include(FetchContent)

find_package(boost_system CONFIG QUIET)
find_package(boost_asio CONFIG QUIET)
find_package(boost_beast CONFIG QUIET)

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

if(boost_system_FOUND AND boost_asio_FOUND AND boost_beast_FOUND AND TARGET Boost::system)
    target_sources(trip_logic PRIVATE
        src/trip_http_server.cpp
        src/trip_http_server_impl_core.cpp
        src/trip_http_server_ws_session.cpp
        src/trip_http_server_http_session.cpp
        src/trip_http_server_routes_common.cpp
        src/trip_http_server_routes.cpp
        src/trip_http_server_routes_auth.cpp
        src/trip_http_server_routes_trip_collab.cpp
        src/trip_http_server_routes_itinerary.cpp
        src/trip_http_server_routes_task_chat.cpp
        src/trip_http_server_routes_budget_sync.cpp
        src/trip_http_server_parse.cpp
        src/trip_http_server_json.cpp
    )
    target_link_libraries(trip_logic PUBLIC Boost::system)
    target_compile_definitions(trip_logic PUBLIC TRIP_HAS_BOOST_SERVER=1)
    if(MSVC)
        target_compile_options(trip_logic PRIVATE /bigobj)
    endif()
else()
    message(WARNING "Boost system/asio/beast not found: Boost-based HTTP server and server tests are disabled.")
endif()

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

set(TRIP_TEST_SOURCES
    tests/trip_service_tests.cpp
)

if(boost_system_FOUND AND boost_asio_FOUND AND boost_beast_FOUND AND TARGET Boost::system)
    list(APPEND TRIP_TEST_SOURCES tests/trip_http_server_tests.cpp)
endif()

add_executable(trip_logic_tests
    ${TRIP_TEST_SOURCES}
)

target_link_libraries(trip_logic_tests
    PRIVATE
        trip_logic
        GTest::gtest_main
)

include(GoogleTest)
gtest_add_tests(
    TARGET trip_logic_tests
    TEST_LIST trip_logic_tests_cases
)