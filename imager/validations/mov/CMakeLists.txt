cmake_minimum_required(VERSION 3.28)
project(mov_validator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

#FFmpeg does not ship CMake config files on all platforms.Try pkg - config first,
#then fall back to find_library / find_path for environments without pkg - config.
find_package(PkgConfig QUIET)

set(FFMPEG_FOUND FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(LibAvFormat QUIET IMPORTED_TARGET libavformat)
    pkg_check_modules(LibAvCodec  QUIET IMPORTED_TARGET libavcodec)
    pkg_check_modules(LibAvUtil   QUIET IMPORTED_TARGET libavutil)
    if(LibAvFormat_FOUND AND LibAvCodec_FOUND AND LibAvUtil_FOUND)
        set(FFMPEG_FOUND TRUE)
        set(FFMPEG_TARGETS PkgConfig::LibAvFormat PkgConfig::LibAvCodec PkgConfig::LibAvUtil)
    endif()
endif()

if(NOT FFMPEG_FOUND)
    find_library(AVFORMAT_LIB NAMES avformat REQUIRED)
    find_library(AVCODEC_LIB  NAMES avcodec  REQUIRED)
    find_library(AVUTIL_LIB   NAMES avutil   REQUIRED)
    find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h REQUIRED)
    set(FFMPEG_TARGETS ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB})
    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
endif()

add_library(mov_validator STATIC mov_validator.cpp)
target_include_directories(mov_validator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(DEFINED FFMPEG_INCLUDE_DIRS)
    target_include_directories(mov_validator PRIVATE ${FFMPEG_INCLUDE_DIRS})
endif()
target_link_libraries(mov_validator PRIVATE ${FFMPEG_TARGETS})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mov_validator PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(sample)
add_subdirectory(test)
