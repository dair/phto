# server/test/CMakeLists.txt — E1 smoke test: GET /health + graceful SIGTERM
#
# Mirrors the configure_file + add_test shell-test pattern from
# imagestore/CMakeLists.txt and imageradmin/CMakeLists.txt.

cmake_minimum_required(VERSION 3.28)

# Guard: skip if curl is not present (the script also guards itself at runtime).
find_program(CURL_PROGRAM curl)
if(NOT CURL_PROGRAM)
    message(STATUS "curl not found — server_health_test will self-skip at runtime")
endif()

# configure_file does NOT evaluate generator expressions, so we inject the
# binary path via the known output location (same pattern imagestore uses:
# "${CMAKE_BINARY_DIR}/imagestore/imagestore").
set(IMAGERD_BIN "${CMAKE_BINARY_DIR}/server/imagerd")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_health.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_health.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_health.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_health_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_health.sh"
)

# Allow enough time for daemon startup + poll + shutdown.
set_tests_properties(server_health_test PROPERTIES TIMEOUT 60)

# ---- server_auth_test — F1 Bearer-token auth end-to-end (shell script) ----

set(IMAGERADMIN_BIN "${CMAKE_BINARY_DIR}/imageradmin/imageradmin")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/test_auth.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/test_auth.sh"
    @ONLY
)

file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/test_auth.sh"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
)

add_test(
    NAME server_auth_test
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_auth.sh"
)

# Allow time for daemon startup + all auth round-trips + shutdown.
set_tests_properties(server_auth_test PROPERTIES TIMEOUT 90)

# ---- server_json_tests — CPPUnit tests for server/Json.{h,cpp} (E2) --------

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
    message(STATUS "CppUnit not found — skipping server_json_tests target")
else()
    add_executable(server_json_tests "${CMAKE_CURRENT_SOURCE_DIR}/JsonTest.cpp")

    target_include_directories(server_json_tests
        PRIVATE ${CPPUNIT_INCLUDE_DIRS}
    )

    target_link_libraries(server_json_tests
        PRIVATE
            server_lib
            ${CPPUNIT_LIBRARIES}
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(server_json_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    include(CTest)
    add_test(NAME server_json_tests COMMAND server_json_tests)
endif()
