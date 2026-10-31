cmake_minimum_required(VERSION 3.28)

add_library(server_lib STATIC
    App.cpp Json.cpp AuthRoutes.cpp UserRoutes.cpp LoginThrottle.cpp ItemRoutes.cpp TagRoutes.cpp
    ClientIp.cpp AccessLog.cpp Upload.cpp)

target_include_directories(server_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_link_libraries(server_lib
    PUBLIC
        Crow::Crow
        libimager
        auth_lib
        config_lib
)

add_executable(imagerd main.cpp)

target_link_libraries(imagerd PRIVATE server_lib)

add_subdirectory(test)
