cmake_minimum_required(VERSION 3.28)

add_executable(imagestore main.cpp ErrorFile.cpp ProgressReporter.cpp)

target_include_directories(imagestore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(imagestore PRIVATE libimager config_lib)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imagestore PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
