cmake_minimum_required(VERSION 3.20)
project(switch_vitrine_core LANGUAGES CXX)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VITRINE_VERSION LIMIT_COUNT 1)
if(NOT VITRINE_VERSION)
    message(FATAL_ERROR "VERSION must contain the application version")
endif()
add_compile_definitions(VITRINE_VERSION="${VITRINE_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vitrine_core
    source/core/catalog.cpp
    source/core/input.cpp
    source/core/update_utils.cpp
)
target_include_directories(vitrine_core PUBLIC include include/core)

enable_testing()
add_executable(catalog_tests tests/catalog_tests.cpp)
target_link_libraries(catalog_tests PRIVATE vitrine_core)
add_test(NAME catalog_tests COMMAND catalog_tests)

option(VITRINE_BUILD_DESKTOP_APP
       "Compile the complete SDL application to validate all refactored modules"
       OFF)

if(VITRINE_BUILD_DESKTOP_APP)
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)

    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
    pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
    pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
    pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)

    add_executable(vitrine_desktop
        source/main.cpp
        source/core/api_client.cpp
        source/core/app.cpp
        source/core/updater.cpp
        source/ui/details_view.cpp
        source/ui/draw_utils.cpp
        source/ui/grid_view.cpp
        source/ui/image_renderer.cpp
        source/ui/layout_view.cpp
        source/ui/panels_view.cpp
        source/ui/text_renderer.cpp
    )
    target_include_directories(vitrine_desktop PRIVATE include/core include/ui)
    target_link_libraries(vitrine_desktop PRIVATE
        vitrine_core
        PkgConfig::SDL2
        PkgConfig::SDL2_IMAGE
        PkgConfig::SDL2_TTF
        PkgConfig::LIBCURL
        PkgConfig::JANSSON
        Threads::Threads
    )
endif()
