cmake_minimum_required(VERSION 3.28)

add_executable(imagestore main.cpp ErrorFile.cpp ProgressReporter.cpp ResultLog.cpp SlotTracker.cpp MemoryReporter.cpp)

option(MEMORY_TRACE "Enable memory allocation tracing (OFF by default)" OFF)
if(MEMORY_TRACE)
    target_sources(imagestore PRIVATE MemoryTrace.cpp)
    target_compile_definitions(imagestore PRIVATE MEMORY_TRACE)
endif()

target_include_directories(imagestore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(imagestore PRIVATE libimager config_lib)

# ---------------------------------------------------------------------------
# CLI tests (shell script, no external test framework needed)
# ---------------------------------------------------------------------------
enable_testing()

set(IMAGESTORE_BIN "${CMAKE_BINARY_DIR}/imagestore/imagestore")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test/test_cli.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_cli.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_cli.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME imagestore_cli_tests
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_cli.sh"
)

# ---------------------------------------------------------------------------
# Memory regression test (Valgrind): runs if valgrind is found on PATH.
# Verifies zero definite/indirect leaks in two scenarios:
#   1. new-file import  (exercises coroutine frame paths A, B, C)
#   2. duplicate import (exercises coroutine frame path A)
# Any definite/indirect leak causes CTest to report FAIL.
# ---------------------------------------------------------------------------
find_program(VALGRIND valgrind)

if(VALGRIND)
    set(VALGRIND_BIN "${VALGRIND}")
    set(VALGRIND_SUPP "${CMAKE_CURRENT_SOURCE_DIR}/../valgrind.supp")
    set(JPEG_FIXTURE
        "${CMAKE_CURRENT_SOURCE_DIR}/../validations/jpeg/test/testimg.jpg")

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/test/test_memcheck.sh.in"
        "${CMAKE_CURRENT_BINARY_DIR}/test_memcheck.sh"
        @ONLY
    )

    file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_memcheck.sh"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
    )

    add_test(
        NAME imagestore_memcheck
        COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_memcheck.sh"
    )

    # Give Valgrind enough time — each scenario takes ~5 s on a dev machine.
    set_tests_properties(imagestore_memcheck PROPERTIES TIMEOUT 120)

    message(STATUS "Valgrind found (${VALGRIND}) — imagestore_memcheck test enabled")
else()
    message(STATUS "Valgrind not found — imagestore_memcheck test skipped")
endif()

# ---------------------------------------------------------------------------
# CPPUnit unit tests for SlotTracker, ResultLog, and DisplayMode components
# ---------------------------------------------------------------------------
find_library(CPPUNIT_LIB cppunit)
find_path(CPPUNIT_INCLUDE cppunit/TestFixture.h)

if(CPPUNIT_LIB AND CPPUNIT_INCLUDE)
    add_executable(imagestore_output_tests
        test/OutputTest.cpp
        SlotTracker.cpp
        ResultLog.cpp
    )

    target_include_directories(imagestore_output_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CPPUNIT_INCLUDE}"
    )

    target_link_libraries(imagestore_output_tests PRIVATE "${CPPUNIT_LIB}")

    add_test(
        NAME imagestore_output_tests
        COMMAND imagestore_output_tests
    )
else()
    message(STATUS "CppUnit not found — skipping imagestore_output_tests")
endif()

# ---------------------------------------------------------------------------
# Streaming-step test: --dry-run output unchanged and memory-bounded for a
# 200 MB file (peak RSS via getrusage), plus the same for a real import.
# ---------------------------------------------------------------------------
set(JPEG_FIXTURE "${CMAKE_SOURCE_DIR}/validations/jpeg/test/testimg.jpg")
set(MOV_FIXTURE "${CMAKE_SOURCE_DIR}/validations/mov/test/fixtures/valid.mov")
set(HEIC_FIXTURE "${CMAKE_SOURCE_DIR}/validations/heic/test/fixtures/valid.heic")
set(AAE_FIXTURE "${CMAKE_SOURCE_DIR}/validations/aae/test/fixtures/valid.aae")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test/test_dryrun_streaming.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_dryrun_streaming.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_dryrun_streaming.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME imagestore_dryrun_streaming_tests
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_dryrun_streaming.sh"
)

set_tests_properties(imagestore_dryrun_streaming_tests PROPERTIES TIMEOUT 300)
