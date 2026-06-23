cmake_minimum_required(VERSION 3.28)

add_library(auth_lib PasswordHash.cpp)

target_include_directories(auth_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(auth_lib
    PUBLIC OpenSSL::Crypto
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(auth_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(test)
