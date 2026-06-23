cmake_minimum_required(VERSION 3.28)

add_executable(imagerd main.cpp App.cpp)

target_include_directories(imagerd PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(imagerd
    PRIVATE
        Crow::Crow
        libimager
        auth_lib
        config_lib
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imagerd PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(test)
