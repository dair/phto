cmake_minimum_required(VERSION 3.28)

add_library(config_lib src/Config.cpp)

target_include_directories(config_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(config_lib
    PRIVATE tomlplusplus::tomlplusplus
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(config_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()
