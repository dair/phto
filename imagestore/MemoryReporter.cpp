#include "MemoryReporter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace imagestore {

namespace {

uint64_t readProcStatusKb(const char* key) {
  std::ifstream in("/proc/self/status");
  if (!in) {
    return 0;
  }

  std::string line;
  while (std::getline(in, line)) {
    if (!line.starts_with(key)) {
      continue;
    }

    size_t pos = line.size();
    while (pos > 0 && !std::isdigit(static_cast<unsigned char>(line[pos - 1]))) {
      --pos;
    }

    size_t start = pos;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(line[start - 1]))) {
      --start;
    }

    if (start == pos) {
      return 0;
    }

    return static_cast<uint64_t>(std::stoull(line.substr(start, pos - start)));
  }

  return 0;
}

std::string formatKiB(uint64_t kib) {
  std::ostringstream out;
  if (kib >= 1024ULL * 1024ULL) {
    out << (kib / (1024ULL * 1024ULL)) << " GiB";
  } else if (kib >= 1024ULL) {
    out << (kib / 1024ULL) << " MiB";
  } else {
    out << kib << " KiB";
  }
  return out.str();
}

std::string formatBytes(uint64_t bytes) {
  return formatKiB((bytes + 1023ULL) / 1024ULL);
}

int64_t inflightFiles(const metrics::Metrics& m) {
  return m.inflight_reading.value() + m.inflight_validating.value() + m.inflight_hashing.value() +
         m.inflight_waiting_mutex.value() + m.inflight_dedup_checking.value() + m.inflight_writing_storage.value() +
         m.inflight_inserting_db.value();
}

int64_t inflightBytes(const metrics::Metrics& m) {
  return m.inflight_reading_bytes.value() + m.inflight_validating_bytes.value() + m.inflight_hashing_bytes.value() +
         m.inflight_waiting_mutex_bytes.value() + m.inflight_dedup_checking_bytes.value() +
         m.inflight_writing_storage_bytes.value() + m.inflight_inserting_db_bytes.value();
}

} // namespace

MemoryReporter::MemoryReporter(
  bool enabled,
  std::chrono::seconds interval,
  const Stats& stats,
  const metrics::Metrics& metrics,
  ResultLog& resultLog
)
  : m_enabled(enabled),
    m_interval(std::max(interval, std::chrono::seconds(1))),
    m_stats(stats),
    m_metrics(metrics),
    m_resultLog(resultLog) {
  if (m_enabled) {
    m_thread = std::thread(&MemoryReporter::loop, this);
  }
}

MemoryReporter::~MemoryReporter() {
  stop();
}

void MemoryReporter::stop() {
  if (!m_enabled) {
    return;
  }
  if (m_stop.exchange(true)) {
    return;
  }
  if (m_thread.joinable()) {
    m_thread.join();
  }
  emitSample(true);
}

void MemoryReporter::loop() {
  while (!m_stop.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(m_interval);
    if (m_stop.load(std::memory_order_relaxed)) {
      break;
    }
    emitSample(false);
  }
}

void MemoryReporter::emitSample(bool finalSample) const {
  const uint64_t rssKb = readProcStatusKb("VmRSS:");
  const uint64_t hwmKb = readProcStatusKb("VmHWM:");
  const int64_t blobBytes = std::max<int64_t>(0, m_metrics.blob_bytes_alive.value());
  const int64_t inflightBytesNow = std::max<int64_t>(0, inflightBytes(m_metrics));

  std::ostringstream out;
  out << (finalSample ? "MEMF " : "MEM  ");
  out << "rss=" << formatKiB(rssKb);
  out << " hwm=" << formatKiB(hwmKb);
  out << " blobs=" << m_metrics.blobs_alive.value() << "/" << formatBytes(static_cast<uint64_t>(blobBytes));
  out << " inflight=" << inflightFiles(m_metrics) << "/" << formatBytes(static_cast<uint64_t>(inflightBytesNow));
  out << " pool=q" << m_metrics.pool_queue_depth.value() << ",a" << m_metrics.pool_active_threads.value();
  out << " proc=" << m_stats.processed.load(std::memory_order_relaxed);
  out << " add=" << m_stats.added.load(std::memory_order_relaxed);
  out << " dup=" << m_stats.duplicates.load(std::memory_order_relaxed);
  out << " err=" << m_stats.errors.load(std::memory_order_relaxed);
  out << " skip=" << m_stats.skipped.load(std::memory_order_relaxed);

  m_resultLog.append(out.str());
}

} // namespace imagestore
