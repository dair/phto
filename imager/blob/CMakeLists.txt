add_library(blob_lib INTERFACE)
target_include_directories(blob_lib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(blob_lib INTERFACE metrics_lib)
