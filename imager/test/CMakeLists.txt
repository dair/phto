cmake_minimum_required(VERSION 3.28)

find_package(PkgConfig QUIET)

set(CPPUNIT_FOUND FALSE)

if(PkgConfig_FOUND)
    pkg_check_modules(CPPUNIT QUIET cppunit)
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
    message(STATUS "CppUnit not found — skipping imager_tests target")
    return()
endif()

find_package(SQLite3 REQUIRED)

find_package(PNG REQUIRED)

add_executable(imager_tests ImagerTest.cpp MultiTargetTest.cpp LargeFileTest.cpp StreamingEquivalenceTest.cpp StreamValidatorEquivalenceTest.cpp HeicTruncationTest.cpp DuplicateIdTest.cpp)

target_include_directories(imager_tests
    PRIVATE ${CPPUNIT_INCLUDE_DIRS}
)

target_compile_definitions(imager_tests PRIVATE
    NEF_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../validations/nef/test/fixtures"
    MOV_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../validations/mov/test/fixtures"
    HEIC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../validations/heic/test/fixtures"
    AAE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../validations/aae/test/fixtures"
    JPEG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../../validations/jpeg/test/testimg.jpg"
)

target_link_libraries(imager_tests
    PRIVATE
        libimager
        SQLite::SQLite3
        ${CPPUNIT_LIBRARIES}
)

include(CTest)
add_test(NAME ImagerTests COMMAND imager_tests)
set_tests_properties(ImagerTests PROPERTIES TIMEOUT 600)

add_executable(timestamp_tests TimestampTest.cpp)

target_include_directories(timestamp_tests
    PRIVATE ${CPPUNIT_INCLUDE_DIRS}
)

target_link_libraries(timestamp_tests
    PRIVATE
        libimager
        ${CPPUNIT_LIBRARIES}
)

add_test(NAME TimestampTests COMMAND timestamp_tests)

# Streaming equivalence uses libpng to synthesise a valid PNG fixture.
target_link_libraries(imager_tests PRIVATE PNG::PNG)

# HeicTruncationTest synthesises valid HEICs with libheif's encoder.
find_package(libheif REQUIRED)
target_link_libraries(imager_tests PRIVATE heif)

# Memory-bound ingest test: own process so VmHWM is not polluted by other suites.
add_executable(imager_memory_tests StreamingMemoryTest.cpp)

target_include_directories(imager_memory_tests
    PRIVATE ${CPPUNIT_INCLUDE_DIRS}
)

target_compile_definitions(imager_memory_tests PRIVATE
    MOV_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../validations/mov/test/fixtures"
)

target_link_libraries(imager_memory_tests
    PRIVATE
        libimager
        ${CPPUNIT_LIBRARIES}
)

add_test(NAME ImagerMemoryTests COMMAND imager_memory_tests)
set_tests_properties(ImagerMemoryTests PROPERTIES TIMEOUT 300)
