cmake_minimum_required(VERSION 3.28)
project(jpeg_validator LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(JPEG REQUIRED)

add_library(jpeg_validator STATIC jpeg_validator.cpp)
target_include_directories(jpeg_validator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_link_libraries(jpeg_validator PRIVATE JPEG::JPEG)

add_subdirectory(sample)
add_subdirectory(test)
