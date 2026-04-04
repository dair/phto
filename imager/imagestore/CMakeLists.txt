cmake_minimum_required(VERSION 3.28)

add_executable(imagestore main.cpp ErrorFile.cpp)

target_include_directories(imagestore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(imagestore PRIVATE libimager config_lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imagestore PRIVATE -Wall -Wextra -Wpedantic)
endif()
