#pragma once

#include <functional>

#include "types/AddResult.h"
#include "types/Blob.h"
#include "types/ErrorCode.h"
#include "types/ImageInfo.h"

namespace imager {

enum class ProcessingStage : uint8_t {
  Reading,
  Validating,
  Hashing,
  WaitingMutex,
  DedupChecking,
  WritingStorage,
  InsertingDb,
};

/// Optional callback invoked when a file transitions to a new pipeline stage.
/// Must be thread-safe — multiple files may call this concurrently.
/// Implementations must be fast (no blocking, no allocation).
using StageCallback = std::function<void(ProcessingStage)>;

} // namespace imager
