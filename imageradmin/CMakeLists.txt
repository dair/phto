cmake_minimum_required(VERSION 3.28)

add_executable(imageradmin main.cpp)

target_link_libraries(imageradmin PRIVATE auth_lib config_lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(imageradmin PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# CLI tests (shell script, no external test framework needed)
# ---------------------------------------------------------------------------
enable_testing()

set(IMAGERADMIN_BIN "${CMAKE_BINARY_DIR}/imageradmin/imageradmin")

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
    NAME imageradmin_cli_tests
    COMMAND bash "${CMAKE_CURRENT_BINARY_DIR}/test_cli.sh"
)
