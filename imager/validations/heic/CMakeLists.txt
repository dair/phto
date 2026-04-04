cmake_minimum_required(VERSION 3.28)
project(heic_validator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

find_package(libheif REQUIRED)

add_library(heic_validator STATIC heic_validator.cpp)
target_include_directories(heic_validator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(heic_validator PRIVATE heif)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(heic_validator PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(sample)
add_subdirectory(test)
