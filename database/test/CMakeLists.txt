cmake_minimum_required(VERSION 3.28)

find_package(PkgConfig QUIET)

# Try to find CppUnit via pkg-config first, then plain find_library
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
    message(STATUS "CppUnit not found — skipping database_tests target")
    return()
endif()

add_executable(database_tests DatabaseTest.cpp)

target_include_directories(database_tests
    PRIVATE ${CPPUNIT_INCLUDE_DIRS}
)

target_link_libraries(database_tests
    PRIVATE
        database
        ${CPPUNIT_LIBRARIES}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(database_tests PRIVATE -Wall -Wextra)
endif()

include(CTest)
add_test(NAME DatabaseTests COMMAND database_tests)
