cmake_minimum_required(VERSION 3.28)

add_library(config_lib Config.cpp)

target_include_directories(config_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(config_lib
    PRIVATE tomlplusplus::tomlplusplus
)

add_subdirectory(test)
