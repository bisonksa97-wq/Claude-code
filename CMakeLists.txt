cmake_minimum_required(VERSION 3.21)
project(UltimatePost VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UP_BUILD_UI "Build the Qt desktop application" ON)
option(UP_BUILD_TESTS "Build the test suite" ON)
option(UP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(Warnings)

find_package(Threads REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)

if(UP_BUILD_UI)
    find_package(Qt6 COMPONENTS Widgets Test QUIET)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt6 not found: the desktop UI will not be built (UP_BUILD_UI=OFF)")
        set(UP_BUILD_UI OFF)
    else()
        find_package(Qt6 COMPONENTS Multimedia QUIET)
    endif()
endif()

add_subdirectory(src)

if(UP_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
