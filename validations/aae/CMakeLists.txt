cmake_minimum_required(VERSION 3.28)
project(aae_validator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_library(aae_validator STATIC aae_validator.cpp)
target_include_directories(aae_validator PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_subdirectory(sample)
add_subdirectory(test)
