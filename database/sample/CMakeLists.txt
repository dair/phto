cmake_minimum_required(VERSION 3.28)

add_executable(dbcli main.cpp)

target_link_libraries(dbcli PRIVATE database)

