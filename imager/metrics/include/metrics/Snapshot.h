#pragma once

#include "Histogram.h"
#include "Counter.h"
#include "Gauge.h"

#include <cstdint>
#include <string>
#include <vector>

namespace metrics {

// ---------------------------------------------------------------------------
// Snapshot types
// ---------------------------------------------------------------------------

struct CounterSnapshot {
    std::string name;
    uint64_t    value{0};
};

struct GaugeSnapshot {
    std::string name;
    int64_t     value{0};
};

// Histogram::Snapshot is defined in Histogram.h

struct FullSnapshot {
    std::vector<Histogram::Snapshot> histograms;
    std::vector<CounterSnapshot>     counters;
    std::vector<GaugeSnapshot>       gauges;
};

// ---------------------------------------------------------------------------
// Text formatter — returns a human-readable report of the full snapshot.
// ---------------------------------------------------------------------------

std::string format(const FullSnapshot& snap);

} // namespace metrics
