cmake_minimum_required(VERSION 3.14...4.2)
project(png_validation CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(libpng)

add_library(png_validation STATIC validate_png.cpp)
target_include_directories(png_validation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(png_validation PRIVATE png_static)

add_subdirectory(sample)
add_subdirectory(test)
