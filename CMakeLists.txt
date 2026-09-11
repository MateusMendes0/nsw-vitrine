cmake_minimum_required(VERSION 3.20)
project(switch_vitrine_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vitrine_core source/catalog.cpp)
target_include_directories(vitrine_core PUBLIC include)

enable_testing()
add_executable(catalog_tests tests/catalog_tests.cpp)
target_link_libraries(catalog_tests PRIVATE vitrine_core)
add_test(NAME catalog_tests COMMAND catalog_tests)

