cmake_minimum_required(VERSION 3.28)

add_library(server_lib STATIC App.cpp Json.cpp AuthRoutes.cpp)

target_include_directories(server_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(server_lib
    PUBLIC
        Crow::Crow
        libimager
        auth_lib
        config_lib
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(server_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(imagerd main.cpp)

target_link_libraries(imagerd PRIVATE server_lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imagerd PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(test)
