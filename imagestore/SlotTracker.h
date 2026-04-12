#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace imagestore {

enum class PipelineStage : uint8_t {
  Reading,
  Validating,
  Hashing,
  WaitingMutex,
  DedupChecking,
  WritingStorage,
  InsertingDb,
  Idle,
};

const char* stageName(PipelineStage stage);

struct SlotInfo {
  std::string filename;
  PipelineStage stage{PipelineStage::Idle};
  std::chrono::steady_clock::time_point stageStart{std::chrono::steady_clock::now()};
};

/// Thread-safe tracker for per-file pipeline stage.
/// Each in-flight file gets a slot (0..maxSlots-1).
/// Workers acquire a slot on entry, update its stage as they progress,
/// and release it on completion. The semaphore in main.cpp guarantees
/// that acquire() always finds a free slot.
class SlotTracker {
public:
  explicit SlotTracker(unsigned int maxSlots);

  SlotTracker(const SlotTracker&) = delete;
  SlotTracker& operator=(const SlotTracker&) = delete;
  SlotTracker(SlotTracker&&) = delete;
  SlotTracker& operator=(SlotTracker&&) = delete;

  /// Acquire a slot for the given filename. Returns slot index.
  unsigned int acquire(const std::string& filename);

  /// Update the pipeline stage for a slot.
  void setStage(unsigned int slot, PipelineStage stage);

  /// Release a slot (marks it idle).
  void release(unsigned int slot);

  /// Snapshot all slots for rendering. Mutex-protected.
  std::vector<SlotInfo> snapshot() const;

private:
  mutable std::mutex m_mutex;
  std::vector<SlotInfo> m_slots;
  unsigned int m_maxSlots;
};

} // namespace imagestore
