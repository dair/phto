option(IMAGER_METRICS_ENABLED "Enable built-in metrics collection" ON)

add_library(metrics_lib STATIC
    Histogram.cpp
    Counter.cpp
    Gauge.cpp
    Timer.cpp
    Snapshot.cpp
    Metrics.cpp
)

target_include_directories(metrics_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if(IMAGER_METRICS_ENABLED)
    target_compile_definitions(metrics_lib PUBLIC IMAGER_METRICS_ENABLED)
endif()

add_subdirectory(test)
