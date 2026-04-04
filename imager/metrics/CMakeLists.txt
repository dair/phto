option(IMAGER_METRICS_ENABLED "Enable built-in metrics collection" ON)

add_library(metrics_lib STATIC
    src/Histogram.cpp
    src/Counter.cpp
    src/Gauge.cpp
    src/Timer.cpp
    src/Snapshot.cpp
    src/Metrics.cpp
)

target_include_directories(metrics_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(IMAGER_METRICS_ENABLED)
    target_compile_definitions(metrics_lib PUBLIC IMAGER_METRICS_ENABLED)
endif()
