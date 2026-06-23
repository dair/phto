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

# --- Phase 2 (plan 0022): HTTP server + JWT auth ---
# Dependency-acquisition policy: prefer Debian packages; FetchContent only where
# no package exists.
#   cpp-jwt : system package libcpp-jwt-dev (pulls OpenSSL + nlohmann_json).
#   Crow    : no Debian package -> FetchContent (pinned). Uses standalone asio
#             from libasio-dev, located by Crow's bundled Findasio.cmake.
find_package(cpp-jwt CONFIG REQUIRED)

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(CROW_INSTALL        OFF CACHE BOOL "" FORCE)
set(CROW_USE_BOOST      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.1.2
)
FetchContent_MakeAvailable(crow)

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
add_subdirectory(validations/aae)
add_subdirectory(config)
add_subdirectory(auth)
add_subdirectory(imager)
add_subdirectory(imagestore)

# TEMPORARY (plan 0022, checkpoint A1): dependency compile/link check for
# Crow + cpp-jwt. Remove/replace when the server/ daemon lands in milestone M-E.
add_subdirectory(depcheck)
