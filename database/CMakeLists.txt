cmake_minimum_required(VERSION 3.28)
project(DatabaseLib LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

# Database library
add_library(database Database.cpp)

target_include_directories(database
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(database
    PRIVATE SQLite::SQLite3
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(database PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Sample CLI application
add_subdirectory(sample)

# Tests (optional — skip if CppUnit is not found)
enable_testing()
add_subdirectory(test)
