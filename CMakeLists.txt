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
    SYSTEM # third-party headers must not trip first-party -Werror
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
    SYSTEM # third-party headers must not trip first-party -Werror
    # Body-sink / header-filter hooks so the server can bound and stream uploads (third_party/crow/*.patch).
    PATCH_COMMAND ${CMAKE_COMMAND}
        "-DPATCH_FILES=${CMAKE_CURRENT_SOURCE_DIR}/third_party/crow/crow-body-sink.patch|${CMAKE_CURRENT_SOURCE_DIR}/third_party/crow/crow-remote-ip.patch"
        -DSOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/third_party/crow/apply_patch.cmake
)
FetchContent_MakeAvailable(crow)
# Native TLS (0022 §10): Crow serves plain HTTP unless ssl_context is configured.
target_compile_definitions(Crow INTERFACE CROW_ENABLE_SSL)
target_link_libraries(Crow INTERFACE OpenSSL::SSL OpenSSL::Crypto)

enable_testing()

# Warning policy for all first-party targets (added after the third-party
# dependencies above, so it does not apply to them). Turn -Werror off with
# -DIMAGER_WERROR=OFF, e.g. when packaging against a newer compiler.
option(IMAGER_WERROR "Treat compiler warnings as errors in first-party targets" ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(IMAGER_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

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
add_subdirectory(imageradmin)
add_subdirectory(server)
