add_library(coro_lib INTERFACE)
target_include_directories(coro_lib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(coro_lib INTERFACE metrics_lib)
