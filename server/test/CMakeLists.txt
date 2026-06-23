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
