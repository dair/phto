#include "SlotTracker.h"

namespace imagestore {

const char* stageName(PipelineStage stage) {
  switch (stage) {
    case PipelineStage::Reading:
      return "reading";
    case PipelineStage::Validating:
      return "validating";
    case PipelineStage::Hashing:
      return "hashing";
    case PipelineStage::WaitingMutex:
      return "waiting";
    case PipelineStage::DedupChecking:
      return "dedup check";
    case PipelineStage::WritingStorage:
      return "writing";
    case PipelineStage::InsertingDb:
      return "db insert";
    case PipelineStage::Idle:
      return "idle";
  }
  return "unknown";
}

SlotTracker::SlotTracker(unsigned int maxSlots)
  : m_maxSlots(maxSlots) {
  m_slots.resize(maxSlots);
}

unsigned int SlotTracker::acquire(const std::string& filename) {
  std::lock_guard<std::mutex> lk(m_mutex);
  for (unsigned int i = 0; i < m_maxSlots; ++i) {
    if (m_slots[i].stage == PipelineStage::Idle) {
      m_slots[i].filename = filename;
      m_slots[i].stage = PipelineStage::Reading;
      m_slots[i].stageStart = std::chrono::steady_clock::now();
      return i;
    }
  }
  // Should not happen: semaphore guarantees ≤ maxSlots concurrent workers.
  return m_maxSlots - 1;
}

void SlotTracker::setStage(unsigned int slot, PipelineStage stage) {
  if (slot >= m_maxSlots) {
    return;
  }
  std::lock_guard<std::mutex> lk(m_mutex);
  m_slots[slot].stage = stage;
  m_slots[slot].stageStart = std::chrono::steady_clock::now();
}

void SlotTracker::release(unsigned int slot) {
  if (slot >= m_maxSlots) {
    return;
  }
  std::lock_guard<std::mutex> lk(m_mutex);
  m_slots[slot].filename.clear();
  m_slots[slot].stage = PipelineStage::Idle;
  m_slots[slot].stageStart = std::chrono::steady_clock::now();
}

std::vector<SlotInfo> SlotTracker::snapshot() const {
  std::lock_guard<std::mutex> lk(m_mutex);
  return m_slots;
}

} // namespace imagestore
