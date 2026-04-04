cmake_minimum_required(VERSION 3.28)
project(Imager LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

include(FetchContent)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

enable_testing()

add_subdirectory(metrics)
add_subdirectory(blob)
add_subdirectory(coro)
add_subdirectory(database)
add_subdirectory(validations/jpeg)
add_subdirectory(validations/png)
add_subdirectory(validations/heic)
add_subdirectory(validations/nef)
add_subdirectory(validations/mov)
add_subdirectory(config)
add_subdirectory(imager)
add_subdirectory(imagestore)
