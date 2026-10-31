cmake_minimum_required(VERSION 3.28)

find_package(SQLite3 REQUIRED)

add_library(auth_lib PasswordHash.cpp UserStore.cpp TokenService.cpp Validation.cpp)

target_include_directories(auth_lib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(auth_lib
    PUBLIC OpenSSL::Crypto SQLite::SQLite3
    PRIVATE cpp-jwt::cpp-jwt
)

add_subdirectory(test)
