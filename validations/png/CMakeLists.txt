cmake_minimum_required(VERSION 3.28)
project(png_validation CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(PNG REQUIRED)

add_library(png_validation STATIC validate_png.cpp)
target_include_directories(png_validation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(png_validation PRIVATE PNG::PNG)

add_subdirectory(sample)
add_subdirectory(test)
