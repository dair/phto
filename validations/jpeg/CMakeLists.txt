cmake_minimum_required(VERSION 3.15)
project(jpeg_validator LANGUAGES C CXX)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(libjpeg)

add_library(jpeg_validator STATIC jpeg_validator.cpp)
target_include_directories(jpeg_validator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jpeg_validator PRIVATE jpeg_static)

add_subdirectory(sample)
add_subdirectory(test)
