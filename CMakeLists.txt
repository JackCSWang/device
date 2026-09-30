cmake_minimum_required(VERSION 3.21)
project(microscope VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Multimedia Test Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
