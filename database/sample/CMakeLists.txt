cmake_minimum_required(VERSION 3.28)

add_executable(dbcli main.cpp)

target_link_libraries(dbcli PRIVATE database)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dbcli PRIVATE -Wall -Wextra -Wpedantic)
endif()
