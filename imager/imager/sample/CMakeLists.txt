cmake_minimum_required(VERSION 3.28)

add_executable(imager_cli main.cpp)

target_link_libraries(imager_cli PRIVATE libimager)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imager_cli PRIVATE -Wall -Wextra)
endif()
