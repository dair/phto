cmake_minimum_required(VERSION 3.28)

add_executable(imager_cli main.cpp)

target_link_libraries(imager_cli PRIVATE libimager)

