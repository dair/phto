cmake_minimum_required(VERSION 3.28)
project(nef_validator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# LibRaw does not ship CMake config files. Try pkg-config first, then fall back
# to find_library / find_path for environments where pkg-config is absent.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LibRaw QUIET IMPORTED_TARGET libraw)
endif()

if(NOT LibRaw_FOUND)
    find_library(LIBRAW_LIBRARY NAMES raw REQUIRED)
    find_path(LIBRAW_INCLUDE_DIR libraw/libraw.h REQUIRED)
    add_library(LibRaw::LibRaw UNKNOWN IMPORTED)
    set_target_properties(LibRaw::LibRaw PROPERTIES
        IMPORTED_LOCATION "${LIBRAW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBRAW_INCLUDE_DIR}"
    )
    set(LIBRAW_TARGET LibRaw::LibRaw)
else()
    set(LIBRAW_TARGET PkgConfig::LibRaw)
endif()

add_library(nef_validator STATIC nef_validator.cpp)
target_include_directories(nef_validator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nef_validator PRIVATE ${LIBRAW_TARGET})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nef_validator PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(sample)
add_subdirectory(test)
