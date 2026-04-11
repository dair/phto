add_library(coro_lib INTERFACE)
target_include_directories(coro_lib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(coro_lib INTERFACE metrics_lib)

add_subdirectory(test)
