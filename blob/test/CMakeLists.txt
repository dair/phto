cmake_minimum_required(VERSION 3.28)

find_package(PkgConfig QUIET)

set(CPPUNIT_FOUND FALSE)

if(PkgConfig_FOUND)
    pkg_check_modules(CPPUNIT QUIET cppunit)
    if(CPPUNIT_FOUND)
        set(CPPUNIT_INCLUDE_DIRS ${CPPUNIT_INCLUDE_DIRS})
        set(CPPUNIT_LIBRARIES    ${CPPUNIT_LIBRARIES})
    endif()
endif()

if(NOT CPPUNIT_FOUND)
    find_library(CPPUNIT_LIB NAMES cppunit)
    find_path(CPPUNIT_INCLUDE_DIR cppunit/TestFixture.h)
    if(CPPUNIT_LIB AND CPPUNIT_INCLUDE_DIR)
        set(CPPUNIT_FOUND TRUE)
        set(CPPUNIT_LIBRARIES    ${CPPUNIT_LIB})
        set(CPPUNIT_INCLUDE_DIRS ${CPPUNIT_INCLUDE_DIR})
    endif()
endif()

if(NOT CPPUNIT_FOUND)
    message(STATUS "CppUnit not found — skipping blob_tests target")
    return()
endif()

add_executable(blob_tests BlobTest.cpp)

target_include_directories(blob_tests
    PRIVATE ${CPPUNIT_INCLUDE_DIRS}
)

target_link_libraries(blob_tests
    PRIVATE
        blob_lib
        metrics_lib
        ${CPPUNIT_LIBRARIES}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(blob_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
add_test(NAME blob_tests COMMAND blob_tests)
