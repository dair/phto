#include <metrics/Snapshot.h>

#include <sstream>

namespace metrics {

namespace detail {

static uint64_t percentile(const Histogram::Snapshot& s, double p) {
  if (s.count == 0) {
    return 0;
  }
  const uint64_t target = static_cast<uint64_t>(s.count * p);
  uint64_t cumulative = 0;
  for (size_t i = 0; i < Histogram::NUM_BUCKETS - 1; ++i) {
    cumulative += s.buckets[i];
    if (cumulative >= target) {
      return Histogram::BOUNDS[i];
    }
  }
  return Histogram::BOUNDS[Histogram::NUM_BUCKETS - 2];
}

static std::string fmtDuration(uint64_t ns) {
  if (ns == 0) {
    return "0ns";
  }
  if (ns < 1'000) {
    return std::to_string(ns) + "ns";
  }
  if (ns < 1'000'000) {
    return std::to_string(ns / 1'000) + "us";
  }
  if (ns < 1'000'000'000) {
    return std::to_string(ns / 1'000'000) + "ms";
  }
  std::ostringstream os;
  os.precision(1);
  os << std::fixed << static_cast<double>(ns) / 1e9 << "s";
  return os.str();
}

static std::string fmtBytes(uint64_t bytes) {
  if (bytes < 1'024) {
    return std::to_string(bytes) + " B";
  }
  if (bytes < 1'024 * 1'024) {
    return std::to_string(bytes / 1'024) + " KB";
  }
  if (bytes < 1'024ULL * 1'024 * 1'024) {
    return std::to_string(bytes / (1'024 * 1'024)) + " MB";
  }
  std::ostringstream os;
  os.precision(1);
  os << std::fixed << static_cast<double>(bytes) / (1'024.0 * 1'024 * 1'024) << " GB";
  return os.str();
}

} // namespace detail

std::string format(const FullSnapshot& snap) {
  std::ostringstream out;
  out << "=== Imager Metrics ===\n\n";

  if (snap.histograms.empty()) {
    return out.str();
  }

  auto printHist = [&](const Histogram::Snapshot& s) {
    if (s.count == 0) {
      out << "  " << s.name << "  (no samples)\n";
      return;
    }
    uint64_t mean = s.sum_ns / s.count;
    out << "  " << s.name << "  count=" << s.count << "  mean=" << detail::fmtDuration(mean)
        << "  p50=" << detail::fmtDuration(detail::percentile(s, 0.50))
        << "  p95=" << detail::fmtDuration(detail::percentile(s, 0.95))
        << "  p99=" << detail::fmtDuration(detail::percentile(s, 0.99))
        << "  max=" << detail::fmtDuration(detail::percentile(s, 1.00)) << "\n";
  };

  out << "Pipeline stages (addImage):\n";
  for (const auto& h : snap.histograms) {
    if (h.name == "addimage_total" || h.name == "validate" || h.name == "hash" || h.name == "dedup_check" ||
        h.name == "storage_write" || h.name == "storage_write_root" || h.name == "db_insert" ||
        h.name == "db_insert_single" || h.name == "mutex_wait") {
      printHist(h);
    }
  }

  out << "\nThread pool:\n";
  for (const auto& h : snap.histograms) {
    if (h.name == "pool_schedule_latency") {
      printHist(h);
    }
  }
  for (const auto& g : snap.gauges) {
    if (g.name == "pool_queue_depth") {
      out << "  pool_queue_depth    current=" << g.value << "\n";
    }
    if (g.name == "pool_active_threads") {
      out << "  pool_active_threads current=" << g.value << "\n";
    }
  }

  out << "\nStorage I/O:\n";
  for (const auto& h : snap.histograms) {
    if (h.name == "storage_read_duration") {
      printHist(h);
    }
  }
  for (const auto& c : snap.counters) {
    if (c.name == "storage_bytes_written") {
      out << "  bytes_written  " << detail::fmtBytes(c.value) << "\n";
    }
    if (c.name == "storage_bytes_read") {
      out << "  bytes_read     " << detail::fmtBytes(c.value) << "\n";
    }
  }

  out << "\nDatabase:\n";
  for (const auto& h : snap.histograms) {
    if (h.name == "db_read_duration" || h.name == "db_write_duration") {
      printHist(h);
    }
  }

  out << "\nMemory:\n";
  for (const auto& g : snap.gauges) {
    if (g.name == "blobs_alive") {
      out << "  blobs_alive       current=" << g.value << "\n";
    }
    if (g.name == "blob_bytes_alive") {
      out << "  blob_bytes_alive  current=" << detail::fmtBytes(static_cast<uint64_t>(g.value > 0 ? g.value : 0))
          << "\n";
    }
  }

  out << "\nThroughput:\n";
  for (const auto& c : snap.counters) {
    if (c.name == "images_added") {
      out << "  images_added   " << c.value << "\n";
    }
    if (c.name == "images_failed") {
      out << "  images_failed  " << c.value << "\n";
    }
  }

  return out.str();
}

} // namespace metrics
