#include "imager/Imager.h"

#include <coro/BlockOn.h>
#include <coro/ThreadPool.h>
#include <coro/WhenAll.h>
#include <imager/ImageValidator.h>
#include <metrics/Gauge.h>
#include <metrics/Metrics.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#include "FileStorage.h"
#include "FileTimestamp.h"
#include "Hasher.h"
#include "MultiDatabase.h"
#include "StreamHasher.h"
#include "Validators.h"

namespace imager {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Files above this size use streaming paths instead of full Blob allocation.
static constexpr uint64_t kStreamingThreshold = 256ULL * 1024ULL * 1024ULL; // 256 MB

// ---------------------------------------------------------------------------
// Pool sizing
// ---------------------------------------------------------------------------

static size_t defaultPoolSize(size_t numTargets) {
  size_t hw = std::thread::hardware_concurrency();
  if (hw == 0) {
    hw = 4;
  }
  // At least 4, at least numTargets, at most 16
  return std::clamp(hw, std::max<size_t>(4u, numTargets), size_t{16});
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Imager::Impl {
  metrics::Metrics metrics; // declared first — dbs and storage hold a reference to it
  coro::ThreadPool pool;
  MultiDatabase dbs;
  FileStorage storage;
  std::vector<std::unique_ptr<validation::IValidator>> validators;
  std::vector<std::unique_ptr<validation::IStreamValidator>> streamValidators;
  std::unordered_map<std::string, uint64_t> fileSizeLimits;
  std::mutex writeMutex;

  explicit Impl(const config::AppConfig& cfg)
    : metrics(),
      pool(defaultPoolSize(cfg.targets.size()), &metrics),
      dbs(cfg.targets, pool, metrics),
      storage(extractRoots(cfg.targets), pool, metrics),
      validators(createDefaultValidators()),
      streamValidators(createDefaultStreamValidators()),
      fileSizeLimits(cfg.fileSizeLimits) {}

  static std::vector<std::filesystem::path> extractRoots(const std::vector<config::TargetConfig>& targets) {
    std::vector<std::filesystem::path> roots;
    roots.reserve(targets.size());
    for (const auto& t : targets) {
      roots.push_back(t.root);
    }
    return roots;
  }

  const validation::IValidator* findValidator(const std::string& ext) const {
    for (const auto& v : validators) {
      if (v->supportsExtension(ext)) {
        return v.get();
      }
    }
    return nullptr;
  }

  const validation::IStreamValidator* findStreamValidator(const std::string& ext) const {
    for (const auto& v : streamValidators) {
      if (v->supportsExtension(ext)) {
        return v.get();
      }
    }
    return nullptr;
  }

  /// Check per-format file size limit. Returns the limit (bytes) if set, or 0 if none.
  uint64_t fileSizeLimit(const std::string& ext) const {
    // ext has leading dot (e.g. ".jpg"); strip it for lookup.
    std::string bare = (ext.size() > 1 && ext[0] == '.') ? ext.substr(1) : ext;
    auto it = fileSizeLimits.find(bare);
    return (it != fileSizeLimits.end()) ? it->second : 0;
  }

  /// Core image-add pipeline for large files (> kStreamingThreshold).
  AddResult addFileLarge(
    const std::filesystem::path& path, const std::string& displayName, uint64_t fileSize, StageCallback onStage
  );

  /// Result of reading + hashing a file from disk.
  struct ReadHashResult {
    std::string id; ///< Hex SHA256; empty on error
    uint64_t fileSize{0};
    std::string error;
    ErrorCode errCode{ErrorCode::Ok};
  };

  /// Read `path` from disk and compute its SHA256 (streaming for large files).
  /// Invokes onStage at Reading and Hashing transitions.
  /// On failure returns a ReadHashResult with empty id and errCode set.
  ReadHashResult readAndHash(const std::filesystem::path& path, StageCallback onStage);

  /// Delete the record for `id` from all databases and storage roots.
  /// MUST be called with writeMutex already held.
  /// Returns Ok, FileNotFound, DatabaseError, or StorageError.
  ErrorCode deleteByIdLocked(const std::string& id);

  static bool isVideoExtension(const std::string& /* ext */) {
    return false; // All previously extension-only formats now have validators
  }

  static std::string lowercaseExt(const std::string& filename) {
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) {
      return {};
    }
    std::string ext = filename.substr(pos);
    for (auto& c : ext) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
  }

  /// Split a path-bearing filename into (sourceDir, bareName).
  /// e.g. "vacation/IMG_1234.JPG" -> ("vacation", "IMG_1234.JPG")
  ///      "IMG_1234.JPG"          -> ("",          "IMG_1234.JPG")
  ///      "/photos/IMG.JPG"       -> ("/photos",   "IMG.JPG")
  static std::pair<std::string, std::string> splitFilename(const std::string& filename) {
    auto sep = filename.rfind('/');
    if (sep == std::string::npos) {
      return {"", filename};
    }
    return {filename.substr(0, sep), filename.substr(sep + 1)};
  }

  /// Extract the base name (lowercased filename without extension) from a bare name.
  /// e.g. "IMG_1234.JPG" -> "img_1234"
  static std::string extractBaseName(const std::string& bareName) {
    auto dot = bareName.rfind('.');
    std::string base = (dot != std::string::npos) ? bareName.substr(0, dot) : bareName;
    for (auto& c : base) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return base;
  }

  /// Returns true if the given extension is a sidecar type.
  static bool isSidecarExtension(const std::string& ext) {
    return ext == ".aae";
    // Future: ".xmp" (Adobe sidecar) could be added here
  }

  /// Returns true if the extension is a still-image type (preferred over video for AAE pairing).
  static bool isImageExtension(const std::string& ext) {
    return ext == ".jpg" || ext == ".jpeg" || ext == ".heic" || ext == ".heif" || ext == ".nef" || ext == ".png";
  }

  static ImageInfo toImageInfo(const db::File& f, std::vector<std::string> tags = {}) {
    return ImageInfo{f.id, f.name, f.size, f.ext, std::move(tags)};
  }

  /// Core image-add pipeline, optionally reporting stage transitions via callback.
  AddResult addImageImpl(const Blob& blob, const std::string& filename, StageCallback onStage);

  /// Fetch tags for a batch of files in parallel and assemble ImageInfo list.
  coro::Task<std::vector<ImageInfo>> enrichWithTags(std::vector<db::File> files);
};

// ---------------------------------------------------------------------------
// Impl::enrichWithTags
// ---------------------------------------------------------------------------

coro::Task<std::vector<ImageInfo>> Imager::Impl::enrichWithTags(std::vector<db::File> files) {
  if (files.empty()) {
    co_return {};
  }

  // One coroutine per file, each fetches tags from m_dbs[0] on a pool thread.
  std::vector<coro::Task<std::vector<std::string>>> tagTasks;
  tagTasks.reserve(files.size());
  for (const auto& f : files) {
    tagTasks.push_back(
      [](coro::ThreadPool& p, MultiDatabase& dbs_, std::string fileId) -> coro::Task<std::vector<std::string>> {
        co_await p.schedule();
        co_return dbs_.getTagsForFile(fileId);
      }(pool, dbs, f.id)
    );
  }

  auto allTags = co_await coro::whenAll(std::move(tagTasks));

  std::vector<ImageInfo> result;
  result.reserve(files.size());
  for (size_t i = 0; i < files.size(); ++i) {
    result.push_back(toImageInfo(files[i], std::move(allTags[i])));
  }
  co_return result;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

Imager::Imager(const config::AppConfig& cfg)
  : m_impl(std::make_unique<Impl>(cfg)) {}

Imager::~Imager() = default;

// ---------------------------------------------------------------------------
// Impl::addImageImpl
// ---------------------------------------------------------------------------

AddResult Imager::Impl::addImageImpl(const Blob& blob, const std::string& filename, StageCallback onStage) {
  metrics::Timer total(metrics.addimage_total);

  // 1. Split filename into (sourceDir, bareName) and extract extension
  auto [sourceDir, bareName] = splitFilename(filename);
  std::string ext = lowercaseExt(bareName);
  if (ext.empty()) {
    return {ErrorCode::UnsupportedFormat, "", "Filename has no extension"};
  }

  const auto* validator = findValidator(ext);
  if (!validator && !isVideoExtension(ext)) {
    return {ErrorCode::UnsupportedFormat, "", "Unsupported format: " + ext};
  }

  // 2+3. Hash (and validate) — parallel when validator present
  std::string id;
  const auto blobSize = static_cast<int64_t>(blob.size());

  if (!validator) {
    if (onStage) {
      onStage(ProcessingStage::Hashing);
    }
    try {
      metrics::SizedGaugeGuard sg(metrics.inflight_hashing, metrics.inflight_hashing_bytes, blobSize);
      metrics::Timer t(metrics.hash);
      id = computeSha256(blob);
    } catch (const std::exception& e) {
      metrics.images_failed.add(1);
      return {ErrorCode::StorageError, "", std::string("Hashing failed: ") + e.what()};
    }
    metrics.stage_hashed.add(1);
    metrics.stage_hashed_bytes.add(blob.size());
  } else {
    if (onStage) {
      onStage(ProcessingStage::Validating);
    }
    validation::ValidationResult valResult;
    try {
      auto [vr, hid] = coro::blockOn(
        pool,
        [](
          coro::ThreadPool& p, const validation::IValidator* v, Blob b, metrics::Metrics& m, int64_t bSz
        ) -> coro::Task<std::pair<validation::ValidationResult, std::string>> {
          validation::ValidationResult vRes;
          std::string hId;

          std::vector<coro::Task<void>> tasks;

          tasks.push_back(
            [](
              coro::ThreadPool& p2,
              const validation::IValidator* v2,
              Blob b2,
              validation::ValidationResult& out,
              metrics::Metrics& m2,
              int64_t sz
            ) -> coro::Task<void> {
              co_await p2.schedule();
              metrics::SizedGaugeGuard sg(m2.inflight_validating, m2.inflight_validating_bytes, sz);
              metrics::Timer t(m2.validate);
              out = v2->validate(b2.data(), b2.size());
            }(p, v, b, vRes, m, bSz)
          );

          tasks.push_back(
            [](coro::ThreadPool& p2, Blob b2, std::string& out, metrics::Metrics& m2, int64_t sz) -> coro::Task<void> {
              co_await p2.schedule();
              metrics::SizedGaugeGuard sg(m2.inflight_hashing, m2.inflight_hashing_bytes, sz);
              metrics::Timer t(m2.hash);
              out = computeSha256(b2);
            }(p, b, hId, m, bSz)
          );

          co_await coro::whenAll(std::move(tasks));
          co_return std::make_pair(vRes, std::move(hId));
        }(pool, validator, blob, metrics, blobSize)
      );

      valResult = vr;
      id = std::move(hid);
    } catch (const std::exception& e) {
      metrics.images_failed.add(1);
      return {ErrorCode::StorageError, "", std::string("Validation/hashing failed: ") + e.what()};
    }

    if (!valResult.valid) {
      metrics.images_failed.add(1);
      return {ErrorCode::BrokenFile, "", valResult.errorMessage};
    }
    metrics.stage_validated.add(1);
    metrics.stage_validated_bytes.add(blob.size());
    metrics.stage_hashed.add(1);
    metrics.stage_hashed_bytes.add(blob.size());
  }

  // Measure mutex wait time separately from the lock guard
  if (onStage) {
    onStage(ProcessingStage::WaitingMutex);
  }
  {
    metrics::SizedGaugeGuard sg(metrics.inflight_waiting_mutex, metrics.inflight_waiting_mutex_bytes, blobSize);
    metrics::Timer t(metrics.mutex_wait);
    writeMutex.lock();
  }
  std::lock_guard<std::mutex> lock(writeMutex, std::adopt_lock);

  // 4. Duplicate check
  if (onStage) {
    onStage(ProcessingStage::DedupChecking);
  }
  try {
    metrics::SizedGaugeGuard sg(metrics.inflight_dedup_checking, metrics.inflight_dedup_checking_bytes, blobSize);
    metrics::Timer t(metrics.dedup_check);
    if (dbs.fileExists(id)) {
      return {ErrorCode::DuplicateFile, "", "File already exists: " + id};
    }
  } catch (const db::DatabaseException& e) {
    metrics.images_failed.add(1);
    return {ErrorCode::DatabaseError, "", e.what()};
  }
  metrics.stage_dedup_checked.add(1);

  // ---- Sidecar path -------------------------------------------------------
  if (isSidecarExtension(ext)) {
    const std::string baseName = extractBaseName(bareName);

    // Look up parent candidate(s) via original_name table
    std::vector<db::File> parents;
    try {
      parents = dbs.getFilesBySourceAndBaseName(sourceDir, baseName);
    } catch (const db::DatabaseException& e) {
      metrics.images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    // Filter out other sidecars from parent candidates
    std::vector<db::File> nonSidecarParents;
    for (const auto& p : parents) {
      if (!isSidecarExtension(p.ext)) {
        nonSidecarParents.push_back(p);
      }
    }

    std::string storageId;
    std::optional<std::string> parentId;

    if (nonSidecarParents.empty()) {
      // Scenario B: orphan — store with own hash
      storageId = id;
      parentId = std::nullopt;
    } else if (nonSidecarParents.size() == 1) {
      // Scenario A: exactly one parent
      storageId = nonSidecarParents[0].id;
      parentId = nonSidecarParents[0].id;
    } else {
      // Multiple candidates — try to disambiguate: prefer image over video
      std::vector<db::File> imageParents;
      for (const auto& p : nonSidecarParents) {
        if (isImageExtension(p.ext)) {
          imageParents.push_back(p);
        }
      }
      if (imageParents.size() == 1) {
        storageId = imageParents[0].id;
        parentId = imageParents[0].id;
      } else {
        // Still ambiguous — reject
        metrics.images_failed.add(1);
        return {
          ErrorCode::AmbiguousSidecar, "", "Ambiguous sidecar: multiple parent files match for '" + bareName + "'"
        };
      }
    }

    // 5. Write sidecar to storage using storageId as the filename prefix
    if (onStage) {
      onStage(ProcessingStage::WritingStorage);
    }
    try {
      metrics::SizedGaugeGuard sg(metrics.inflight_writing_storage, metrics.inflight_writing_storage_bytes, blobSize);
      metrics::Timer t(metrics.storage_write);
      coro::blockOn(pool, storage.writeFileAsync(storageId, ext, blob));
    } catch (const std::exception& e) {
      metrics.images_failed.add(1);
      return {ErrorCode::StorageError, "", std::string("Storage write failed: ") + e.what()};
    }
    metrics.stage_stored.add(1);

    // 6. Insert file record (bare name only, not full path)
    if (onStage) {
      onStage(ProcessingStage::InsertingDb);
    }
    try {
      metrics::SizedGaugeGuard sg(metrics.inflight_inserting_db, metrics.inflight_inserting_db_bytes, blobSize);
      metrics::Timer t(metrics.db_insert);
      dbs.addFile(id, bareName, blob.size(), ext);
    } catch (const db::DatabaseException& e) {
      if (e.code() == db::DatabaseErrorCode::ConstraintViolation) {
        // Clean up storage — already written with storageId
        coro::blockOn(pool, storage.deleteFileAsync(storageId, ext));
        return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
      }
      coro::blockOn(pool, storage.deleteFileAsync(storageId, ext));
      metrics.images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }
    metrics.stage_db_inserted.add(1);
    metrics.stage_db_inserted_bytes.add(blob.size());

    // 7. Insert original_name entry
    try {
      dbs.addOriginalName(sourceDir, baseName, id);
    } catch (const db::DatabaseException& e) {
      // Best effort rollback
      try {
        dbs.deleteFile(id);
      } catch (...) {}
      coro::blockOn(pool, storage.deleteFileAsync(storageId, ext));
      metrics.images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    // 8. Insert file_companion entry
    try {
      dbs.addCompanion(id, parentId, storageId);
    } catch (const db::DatabaseException& e) {
      try {
        // deleteFile cascades via ON DELETE CASCADE to both original_name and
        // file_companion, so no separate original_name deletion is needed.
        dbs.deleteFile(id);
      } catch (...) {}
      coro::blockOn(pool, storage.deleteFileAsync(storageId, ext));
      metrics.images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    metrics.images_added.add(1);
    return {ErrorCode::Ok, id, ""};
  }

  // ---- Non-sidecar path ---------------------------------------------------

  // 5. Write to all storage roots in parallel
  if (onStage) {
    onStage(ProcessingStage::WritingStorage);
  }
  try {
    metrics::SizedGaugeGuard sg(metrics.inflight_writing_storage, metrics.inflight_writing_storage_bytes, blobSize);
    metrics::Timer t(metrics.storage_write);
    coro::blockOn(pool, storage.writeFileAsync(id, ext, blob));
  } catch (const std::exception& e) {
    metrics.images_failed.add(1);
    return {ErrorCode::StorageError, "", std::string("Storage write failed: ") + e.what()};
  }
  metrics.stage_stored.add(1);
  // storage_bytes_written already incremented inside writeToRoot

  // 6. Insert into all databases in parallel (store bare name)
  if (onStage) {
    onStage(ProcessingStage::InsertingDb);
  }
  try {
    metrics::SizedGaugeGuard sg(metrics.inflight_inserting_db, metrics.inflight_inserting_db_bytes, blobSize);
    metrics::Timer t(metrics.db_insert);
    dbs.addFile(id, bareName, blob.size(), ext);
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::ConstraintViolation) {
      return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
    }
    // Roll back storage
    coro::blockOn(pool, storage.deleteFileAsync(id, ext));
    metrics.images_failed.add(1);
    return {ErrorCode::DatabaseError, "", e.what()};
  }
  metrics.stage_db_inserted.add(1);
  metrics.stage_db_inserted_bytes.add(blob.size());

  // 7. Insert original_name entry so sidecars can find this file as a parent
  const std::string baseName = extractBaseName(bareName);
  try {
    dbs.addOriginalName(sourceDir, baseName, id);
  } catch (const db::DatabaseException&) {
    // Non-fatal: original_name uses INSERT OR IGNORE, but if something else
    // goes wrong, don't fail the whole addImageImpl — the file is already stored.
  }

  // 8. Resolve orphan sidecars that were waiting for this parent
  try {
    auto orphans = dbs.getOrphanCompanionsBySourceAndBaseName(sourceDir, baseName);
    for (const auto& orphan : orphans) {
      // Get the sidecar's extension from the file record
      auto sidecarFile = dbs.getFile(orphan.fileId);
      if (!sidecarFile) {
        continue;
      }
      // Relocate sidecar file on disk: old storage path -> new storage path
      try {
        coro::blockOn(pool, storage.relocateFileAsync(orphan.storageId, id, sidecarFile->ext));
      } catch (const std::exception&) {
        continue; // Best-effort relocation; don't fail parent add
      }
      // Update companion record to point to this parent
      try {
        dbs.updateCompanionParent(orphan.fileId, id, id);
      } catch (const db::DatabaseException&) {
        // Best-effort
      }
    }
  } catch (const db::DatabaseException&) {
    // Best-effort orphan resolution; don't fail parent add
  }

  metrics.images_added.add(1);
  return {ErrorCode::Ok, id, ""};
}

// ---------------------------------------------------------------------------
// addImage
// ---------------------------------------------------------------------------

AddResult Imager::addImage(const Blob& blob, const std::string& filename) {
  return m_impl->addImageImpl(blob, filename, nullptr);
}

// ---------------------------------------------------------------------------
// addFile
// ---------------------------------------------------------------------------

AddResult Imager::addFile(const std::filesystem::path& path, const std::string& filename, StageCallback onStage) {
  std::string displayName = filename.empty() ? path.filename().string() : filename;

  // Determine file size and extension before reading.
  std::error_code ec;
  auto fileSize = std::filesystem::file_size(path, ec);
  if (ec) {
    m_impl->metrics.images_failed.add(1);
    return {ErrorCode::FileNotFound, "", "Cannot stat: " + path.string() + ": " + ec.message()};
  }

  // Extension from display name (which may differ from path for renamed files).
  std::string ext = Impl::lowercaseExt(displayName);

  // Step 1: Per-format size limit check — reject immediately if exceeded.
  uint64_t limit = m_impl->fileSizeLimit(ext);
  if (limit > 0 && fileSize > limit) {
    m_impl->metrics.images_failed.add(1);
    return {
      ErrorCode::TooLarge,
      "",
      "File too large for format: " + (ext.empty() ? "unknown" : ext.substr(1)) + " file is " +
        std::to_string(fileSize) + " bytes, max allowed is " + std::to_string(limit) + " bytes"
    };
  }

  // Step 2: Route to streaming path for large files.
  if (fileSize > kStreamingThreshold) {
    return m_impl->addFileLarge(path, displayName, fileSize, std::move(onStage));
  }

  if (onStage) {
    onStage(ProcessingStage::Reading);
  }

  // Read source timestamps BEFORE opening the file.
  // Opening/reading the file updates atime; capturing here preserves the original.
  struct timespec srcTimes[2]{};
  bool haveTimestamps = false;
  try {
    readTimestamps(path, srcTimes);
    haveTimestamps = true;
  } catch (...) {
    // Non-fatal: proceed without timestamps if stat fails for any reason.
  }

  // Stage 0: file read — tracked by metrics.
  // inflight_reading (file count) is incremented immediately.
  // inflight_reading_bytes is incremented after stat (once we know the size).
  Blob blob;
  {
    m_impl->metrics.inflight_reading.increment();

    const auto blobSz = static_cast<int64_t>(fileSize);
    m_impl->metrics.inflight_reading_bytes.add(blobSz);
    metrics::Timer t(m_impl->metrics.file_read);

    blob = Blob(fileSize, &m_impl->metrics);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      m_impl->metrics.inflight_reading.decrement();
      m_impl->metrics.inflight_reading_bytes.add(-blobSz);
      m_impl->metrics.images_failed.add(1);
      return {ErrorCode::FileNotFound, "", "Cannot open: " + path.string()};
    }
    if (fileSize > 0) {
      in.read(reinterpret_cast<char*>(blob.writableData()), static_cast<std::streamsize>(fileSize));
      if (in.fail() && !in.eof()) {
        m_impl->metrics.inflight_reading.decrement();
        m_impl->metrics.inflight_reading_bytes.add(-blobSz);
        m_impl->metrics.images_failed.add(1);
        return {ErrorCode::StorageError, "", "Read error: " + path.string()};
      }
    }
    blob.freeze();
    m_impl->metrics.inflight_reading.decrement();
    m_impl->metrics.inflight_reading_bytes.add(-blobSz);
  }
  m_impl->metrics.stage_read.add(1);
  m_impl->metrics.stage_read_bytes.add(blob.size());

  AddResult result = m_impl->addImageImpl(blob, displayName, std::move(onStage));

  // Preserve original file timestamps on the stored copy.
  // Use the pre-read timestamps captured before file open (which would have updated atime).
  // Applied only on success; skipped for DuplicateFile and error cases.
  if (result.code == ErrorCode::Ok && haveTimestamps) {
    m_impl->storage.applyTimestamps(result.id, ext, srcTimes);
  }

  return result;
}

// ---------------------------------------------------------------------------
// Impl::addFileLarge — streaming pipeline for files > kStreamingThreshold
// ---------------------------------------------------------------------------

AddResult Imager::Impl::addFileLarge(
  const std::filesystem::path& path, const std::string& displayName, uint64_t fileSize, StageCallback onStage
) {
  auto [sourceDir, bareName] = splitFilename(displayName);
  std::string ext = lowercaseExt(bareName);
  if (ext.empty()) {
    metrics.images_failed.add(1);
    return {ErrorCode::UnsupportedFormat, "", "Filename has no extension"};
  }

  // Read source timestamps BEFORE opening the file (open updates atime).
  struct timespec srcTimes[2]{};
  bool haveTimestamps = false;
  try {
    readTimestamps(path, srcTimes);
    haveTimestamps = true;
  } catch (...) {
    // Non-fatal.
  }

  const auto* streamValidator = findStreamValidator(ext);
  if (!streamValidator) {
    // No stream validator available — fall back to Blob path.
    // If allocation fails, the error propagates as StorageError.
    metrics.inflight_reading.increment();
    const auto blobSz = static_cast<int64_t>(fileSize);
    metrics.inflight_reading_bytes.add(blobSz);
    metrics::Timer t(metrics.file_read);

    Blob blob;
    try {
      blob = Blob(fileSize, &metrics);
    } catch (const std::bad_alloc&) {
      metrics.inflight_reading.decrement();
      metrics.inflight_reading_bytes.add(-blobSz);
      metrics.images_failed.add(1);
      return {
        ErrorCode::StorageError,
        "",
        "File too large to validate in memory and no streaming validator available for " + ext
      };
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      metrics.inflight_reading.decrement();
      metrics.inflight_reading_bytes.add(-blobSz);
      metrics.images_failed.add(1);
      return {ErrorCode::FileNotFound, "", "Cannot open: " + path.string()};
    }
    if (fileSize > 0) {
      in.read(reinterpret_cast<char*>(blob.writableData()), static_cast<std::streamsize>(fileSize));
      if (in.fail() && !in.eof()) {
        metrics.inflight_reading.decrement();
        metrics.inflight_reading_bytes.add(-blobSz);
        metrics.images_failed.add(1);
        return {ErrorCode::StorageError, "", "Read error: " + path.string()};
      }
    }
    blob.freeze();
    metrics.inflight_reading.decrement();
    metrics.inflight_reading_bytes.add(-blobSz);
    metrics.stage_read.add(1);
    metrics.stage_read_bytes.add(blob.size());
    AddResult result = addImageImpl(blob, displayName, std::move(onStage));
    if (result.code == ErrorCode::Ok && haveTimestamps) {
      storage.applyTimestamps(result.id, ext, srcTimes);
    }
    return result;
  }

  // ----- Pass 1: Hash + Validate in parallel (streamed) -----

  if (onStage) {
    onStage(ProcessingStage::Hashing);
  }

  std::string id;
  validation::ValidationResult valResult;

  try {
    auto [hash, vr] = coro::blockOn(
      pool,
      [](
        coro::ThreadPool& p, const validation::IStreamValidator* sv, std::filesystem::path filePath_
      ) -> coro::Task<std::pair<std::string, validation::ValidationResult>> {
        std::string hashId;
        validation::ValidationResult vRes;

        std::vector<coro::Task<void>> tasks;

        // Hash coroutine: stream 4 MB chunks through StreamHasher.
        tasks.push_back([](coro::ThreadPool& p2, std::filesystem::path fp, std::string& out) -> coro::Task<void> {
          co_await p2.schedule();
          static constexpr size_t CHUNK = 4ULL * 1024ULL * 1024ULL;
          std::ifstream in(fp, std::ios::binary);
          if (!in) {
            throw std::runtime_error("Cannot open for hashing: " + fp.string());
          }
          StreamHasher hasher;
          std::vector<uint8_t> buf(CHUNK);
          while (in) {
            in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
            auto n = static_cast<size_t>(in.gcount());
            if (n > 0) {
              hasher.update(buf.data(), n);
            }
          }
          if (in.bad()) {
            throw std::runtime_error("Read error while hashing: " + fp.string());
          }
          out = hasher.finalize();
        }(p, filePath_, hashId));

        // Validate coroutine: stream via library file API.
        tasks.push_back(
          [](
            coro::ThreadPool& p2,
            const validation::IStreamValidator* sv2,
            std::filesystem::path fp,
            validation::ValidationResult& out
          ) -> coro::Task<void> {
            co_await p2.schedule();
            out = sv2->validateFile(fp);
          }(p, sv, filePath_, vRes)
        );

        co_await coro::whenAll(std::move(tasks));
        co_return std::make_pair(std::move(hashId), vRes);
      }(pool, streamValidator, path)
    );

    id = std::move(hash);
    valResult = vr;
  } catch (const std::exception& e) {
    metrics.images_failed.add(1);
    return {ErrorCode::StorageError, "", std::string("Streaming hash/validate failed: ") + e.what()};
  }

  if (!valResult.valid) {
    metrics.images_failed.add(1);
    return {ErrorCode::BrokenFile, "", valResult.errorMessage};
  }
  metrics.stage_validated.add(1);
  metrics.stage_validated_bytes.add(fileSize);
  metrics.stage_hashed.add(1);
  metrics.stage_hashed_bytes.add(fileSize);

  const auto fileSz = static_cast<int64_t>(fileSize);

  // Mutex wait
  if (onStage) {
    onStage(ProcessingStage::WaitingMutex);
  }
  {
    metrics::SizedGaugeGuard sg(metrics.inflight_waiting_mutex, metrics.inflight_waiting_mutex_bytes, fileSz);
    metrics::Timer t(metrics.mutex_wait);
    writeMutex.lock();
  }
  std::lock_guard<std::mutex> lock(writeMutex, std::adopt_lock);

  // Dedup check
  if (onStage) {
    onStage(ProcessingStage::DedupChecking);
  }
  try {
    metrics::SizedGaugeGuard sg(metrics.inflight_dedup_checking, metrics.inflight_dedup_checking_bytes, fileSz);
    metrics::Timer t(metrics.dedup_check);
    if (dbs.fileExists(id)) {
      return {ErrorCode::DuplicateFile, "", "File already exists: " + id};
    }
  } catch (const db::DatabaseException& e) {
    metrics.images_failed.add(1);
    return {ErrorCode::DatabaseError, "", e.what()};
  }
  metrics.stage_dedup_checked.add(1);

  // ----- Pass 2: Streaming storage write -----
  if (onStage) {
    onStage(ProcessingStage::WritingStorage);
  }
  try {
    metrics::SizedGaugeGuard sg(metrics.inflight_writing_storage, metrics.inflight_writing_storage_bytes, fileSz);
    metrics::Timer t(metrics.storage_write);
    coro::blockOn(pool, storage.writeFileFromDiskAsync(id, ext, path));
  } catch (const std::exception& e) {
    metrics.images_failed.add(1);
    return {ErrorCode::StorageError, "", std::string("Streaming storage write failed: ") + e.what()};
  }
  metrics.stage_stored.add(1);

  // DB insert
  if (onStage) {
    onStage(ProcessingStage::InsertingDb);
  }
  try {
    metrics::SizedGaugeGuard sg(metrics.inflight_inserting_db, metrics.inflight_inserting_db_bytes, fileSz);
    metrics::Timer t(metrics.db_insert);
    dbs.addFile(id, bareName, fileSize, ext);
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::ConstraintViolation) {
      return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
    }
    coro::blockOn(pool, storage.deleteFileAsync(id, ext));
    metrics.images_failed.add(1);
    return {ErrorCode::DatabaseError, "", e.what()};
  }
  metrics.stage_db_inserted.add(1);
  metrics.stage_db_inserted_bytes.add(fileSize);

  // original_name entry for sidecar resolution
  const std::string baseName = extractBaseName(bareName);
  try {
    dbs.addOriginalName(sourceDir, baseName, id);
  } catch (const db::DatabaseException&) {
    // Non-fatal: best-effort
  }

  // Orphan sidecar relocation (same as small-file path)
  try {
    auto orphans = dbs.getOrphanCompanionsBySourceAndBaseName(sourceDir, baseName);
    for (const auto& orphan : orphans) {
      auto sidecarFile = dbs.getFile(orphan.fileId);
      if (!sidecarFile) {
        continue;
      }
      try {
        coro::blockOn(pool, storage.relocateFileAsync(orphan.storageId, id, sidecarFile->ext));
      } catch (const std::exception&) {
        continue;
      }
      try {
        dbs.updateCompanionParent(orphan.fileId, id, id);
      } catch (const db::DatabaseException&) {
        // Best-effort
      }
    }
  } catch (const db::DatabaseException&) {
    // Best-effort
  }

  metrics.images_added.add(1);

  // Preserve original file timestamps on all stored copies.
  // Use pre-read timestamps captured before any file open (which would update atime).
  if (haveTimestamps) {
    storage.applyTimestamps(id, ext, srcTimes);
  }

  return {ErrorCode::Ok, id, ""};
}

// ---------------------------------------------------------------------------
// validateOnlyFile
// ---------------------------------------------------------------------------

AddResult Imager::validateOnlyFile(const std::filesystem::path& path, const std::string& filename) {
  std::string displayName = filename.empty() ? path.filename().string() : filename;

  std::error_code ec;
  auto fileSize = std::filesystem::file_size(path, ec);
  if (ec) {
    return {ErrorCode::FileNotFound, "", "Cannot stat: " + path.string() + ": " + ec.message()};
  }

  std::string ext = Impl::lowercaseExt(displayName);

  // Size limit check
  uint64_t limit = m_impl->fileSizeLimit(ext);
  if (limit > 0 && fileSize > limit) {
    return {
      ErrorCode::TooLarge,
      "",
      "File too large for format: " + (ext.empty() ? "unknown" : ext.substr(1)) + " file is " +
        std::to_string(fileSize) + " bytes, max allowed is " + std::to_string(limit) + " bytes"
    };
  }

  // Small file: load as Blob and use existing validateOnly
  if (fileSize <= kStreamingThreshold) {
    Blob blob;
    try {
      blob = Blob(fileSize, &m_impl->metrics);
    } catch (const std::bad_alloc&) {
      return {ErrorCode::StorageError, "", "Allocation failed for: " + path.string()};
    }
    if (fileSize > 0) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return {ErrorCode::FileNotFound, "", "Cannot open: " + path.string()};
      }
      in.read(reinterpret_cast<char*>(blob.writableData()), static_cast<std::streamsize>(fileSize));
      if (in.fail() && !in.eof()) {
        return {ErrorCode::StorageError, "", "Read error: " + path.string()};
      }
    }
    blob.freeze();
    return validateOnly(blob, displayName);
  }

  // Large file: streaming hash + streaming validate
  const auto* streamValidator = m_impl->findStreamValidator(ext);
  if (!streamValidator) {
    return {ErrorCode::UnsupportedFormat, "", "No streaming validator for large " + ext + " files"};
  }

  std::string id;
  validation::ValidationResult valResult;

  try {
    auto [hash, vr] = coro::blockOn(
      m_impl->pool,
      [](
        coro::ThreadPool& p, const validation::IStreamValidator* sv, std::filesystem::path filePath_
      ) -> coro::Task<std::pair<std::string, validation::ValidationResult>> {
        std::string hashId;
        validation::ValidationResult vRes;

        std::vector<coro::Task<void>> tasks;

        tasks.push_back([](coro::ThreadPool& p2, std::filesystem::path fp, std::string& out) -> coro::Task<void> {
          co_await p2.schedule();
          static constexpr size_t CHUNK = 4ULL * 1024ULL * 1024ULL;
          std::ifstream in(fp, std::ios::binary);
          if (!in) {
            throw std::runtime_error("Cannot open for hashing: " + fp.string());
          }
          StreamHasher hasher;
          std::vector<uint8_t> buf(CHUNK);
          while (in) {
            in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
            auto n = static_cast<size_t>(in.gcount());
            if (n > 0) {
              hasher.update(buf.data(), n);
            }
          }
          if (in.bad()) {
            throw std::runtime_error("Read error while hashing: " + fp.string());
          }
          out = hasher.finalize();
        }(p, filePath_, hashId));

        tasks.push_back(
          [](
            coro::ThreadPool& p2,
            const validation::IStreamValidator* sv2,
            std::filesystem::path fp,
            validation::ValidationResult& out
          ) -> coro::Task<void> {
            co_await p2.schedule();
            out = sv2->validateFile(fp);
          }(p, sv, filePath_, vRes)
        );

        co_await coro::whenAll(std::move(tasks));
        co_return std::make_pair(std::move(hashId), vRes);
      }(m_impl->pool, streamValidator, path)
    );

    id = std::move(hash);
    valResult = vr;
  } catch (const std::exception& e) {
    return {ErrorCode::StorageError, "", std::string("Streaming validation failed: ") + e.what()};
  }

  if (!valResult.valid) {
    return {ErrorCode::BrokenFile, "", valResult.errorMessage};
  }

  // Dedup check (read-only)
  try {
    if (m_impl->dbs.fileExists(id)) {
      return {ErrorCode::DuplicateFile, id, "File already exists: " + id};
    }
  } catch (const db::DatabaseException& e) {
    return {ErrorCode::DatabaseError, "", e.what()};
  }

  return {ErrorCode::Ok, id, ""};
}

// ---------------------------------------------------------------------------
// validateOnly
// ---------------------------------------------------------------------------

AddResult Imager::validateOnly(const Blob& blob, const std::string& filename) {
  // 1. Extract & lowercase extension
  std::string ext = Impl::lowercaseExt(filename);
  if (ext.empty()) {
    return {ErrorCode::UnsupportedFormat, "", "Filename has no extension"};
  }

  const auto* validator = m_impl->findValidator(ext);
  if (!validator && !Impl::isVideoExtension(ext)) {
    return {ErrorCode::UnsupportedFormat, "", "Unsupported format: " + ext};
  }

  // 2+3. Hash (and validate for images) — parallel when validator present
  std::string id;

  if (!validator) {
    try {
      id = computeSha256(blob);
    } catch (const std::exception& e) {
      return {ErrorCode::StorageError, "", std::string("Hashing failed: ") + e.what()};
    }
  } else {
    validation::ValidationResult valResult;
    try {
      auto [vr, hid] = coro::blockOn(
        m_impl->pool,
        [](
          coro::ThreadPool& p, const validation::IValidator* v, Blob b
        ) -> coro::Task<std::pair<validation::ValidationResult, std::string>> {
          validation::ValidationResult vRes;
          std::string hId;

          std::vector<coro::Task<void>> tasks;

          tasks.push_back(
            [](
              coro::ThreadPool& p2, const validation::IValidator* v2, Blob b2, validation::ValidationResult& out
            ) -> coro::Task<void> {
              co_await p2.schedule();
              out = v2->validate(b2.data(), b2.size());
            }(p, v, b, vRes)
          );

          tasks.push_back([](coro::ThreadPool& p2, Blob b2, std::string& out) -> coro::Task<void> {
            co_await p2.schedule();
            out = computeSha256(b2);
          }(p, b, hId));

          co_await coro::whenAll(std::move(tasks));
          co_return std::make_pair(vRes, std::move(hId));
        }(m_impl->pool, validator, blob)
      );

      valResult = vr;
      id = std::move(hid);
    } catch (const std::exception& e) {
      return {ErrorCode::StorageError, "", std::string("Validation/hashing failed: ") + e.what()};
    }

    if (!valResult.valid) {
      return {ErrorCode::BrokenFile, "", valResult.errorMessage};
    }
  }

  // 4. Duplicate check (read-only — no mutex needed)
  try {
    if (m_impl->dbs.fileExists(id)) {
      return {ErrorCode::DuplicateFile, id, "File already exists: " + id};
    }
  } catch (const db::DatabaseException& e) {
    return {ErrorCode::DatabaseError, "", e.what()};
  }

  return {ErrorCode::Ok, id, ""};
}

// ---------------------------------------------------------------------------
// getImage
// ---------------------------------------------------------------------------

std::optional<ImageInfo> Imager::getImage(const std::string& id) {
  try {
    auto f = m_impl->dbs.getFile(id);
    if (!f) {
      return std::nullopt;
    }
    auto tags = m_impl->dbs.getTagsForFile(id);
    return Impl::toImageInfo(*f, std::move(tags));
  } catch (const db::DatabaseException&) {
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// getImagesByTags — parallel tag fan-out
// ---------------------------------------------------------------------------

std::vector<ImageInfo> Imager::getImagesByTags(const std::vector<std::string>& tags, uint32_t offset, uint32_t limit) {
  if (tags.empty()) {
    return {};
  }
  try {
    auto files = m_impl->dbs.getFilesByTags(tags, db::Pagination{offset, limit});
    return coro::blockOn(m_impl->pool, m_impl->enrichWithTags(std::move(files)));
  } catch (const db::DatabaseException&) {
    return {};
  }
}

// ---------------------------------------------------------------------------
// Impl::deleteByIdLocked — core delete logic (writeMutex must be held)
// ---------------------------------------------------------------------------

ErrorCode Imager::Impl::deleteByIdLocked(const std::string& id) {
  std::optional<db::File> file;
  try {
    file = dbs.getFile(id);
  } catch (const db::DatabaseException&) {
    return ErrorCode::DatabaseError;
  }

  if (!file) {
    return ErrorCode::FileNotFound;
  }

  const std::string ext = file->ext;

  // Cascade: collect all sidecars of this parent before deleting
  std::vector<db::Database::CompanionInfo> companions;
  try {
    companions = dbs.getCompanionsForParent(id);
  } catch (const db::DatabaseException&) {
    // Best-effort; proceed with deletion
  }

  // Delete each sidecar's DB record (cascades file_companion, original_name)
  // and its storage file (using storage_id to find the correct disk path)
  for (const auto& comp : companions) {
    auto sidecarFile = dbs.getFile(comp.fileId);
    if (sidecarFile) {
      try {
        dbs.deleteFile(comp.fileId);
      } catch (const db::DatabaseException&) {
        // Best-effort
      }
      coro::blockOn(pool, storage.deleteFileAsync(comp.storageId, sidecarFile->ext));
    }
  }

  try {
    dbs.deleteFile(id);
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::NotFound) {
      return ErrorCode::FileNotFound;
    }
    return ErrorCode::DatabaseError;
  }

  // Storage cleanup in parallel (best-effort)
  coro::blockOn(pool, storage.deleteFileAsync(id, ext));
  return ErrorCode::Ok;
}

// ---------------------------------------------------------------------------
// deleteImage — thin wrapper: lock + delegate to deleteByIdLocked
// ---------------------------------------------------------------------------

ErrorCode Imager::deleteImage(const std::string& id) {
  std::lock_guard<std::mutex> lock(m_impl->writeMutex);
  return m_impl->deleteByIdLocked(id);
}

// ---------------------------------------------------------------------------
// Impl::readAndHash — read file + compute SHA256 (streaming for large files)
// ---------------------------------------------------------------------------

Imager::Impl::ReadHashResult Imager::Impl::readAndHash(const std::filesystem::path& path, StageCallback onStage) {
  std::error_code ec;
  auto fileSize = std::filesystem::file_size(path, ec);
  if (ec) {
    return {"", 0, "Cannot stat: " + path.string() + ": " + ec.message(), ErrorCode::FileNotFound};
  }

  if (onStage) {
    onStage(ProcessingStage::Reading);
  }

  if (fileSize <= kStreamingThreshold) {
    // Small file: read into Blob, hash single-shot
    Blob blob;
    try {
      blob = Blob(fileSize, &metrics);
    } catch (const std::bad_alloc&) {
      return {"", 0, "Allocation failed for: " + path.string(), ErrorCode::StorageError};
    }
    if (fileSize > 0) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return {"", 0, "Cannot open: " + path.string(), ErrorCode::FileNotFound};
      }
      in.read(reinterpret_cast<char*>(blob.writableData()), static_cast<std::streamsize>(fileSize));
      if (in.fail() && !in.eof()) {
        return {"", 0, "Read error: " + path.string(), ErrorCode::StorageError};
      }
    }
    blob.freeze();

    if (onStage) {
      onStage(ProcessingStage::Hashing);
    }

    std::string id;
    try {
      id = computeSha256(blob);
    } catch (const std::exception& e) {
      return {"", 0, std::string("Hashing failed: ") + e.what(), ErrorCode::StorageError};
    }
    return {std::move(id), fileSize, "", ErrorCode::Ok};
  }

  // Large file: stream 4 MB chunks through StreamHasher
  if (onStage) {
    onStage(ProcessingStage::Hashing);
  }

  std::string id;
  try {
    id = coro::blockOn(pool, [](coro::ThreadPool& p, std::filesystem::path fp) -> coro::Task<std::string> {
      co_await p.schedule();
      static constexpr size_t CHUNK = 4ULL * 1024ULL * 1024ULL;
      std::ifstream in(fp, std::ios::binary);
      if (!in) {
        throw std::runtime_error("Cannot open for hashing: " + fp.string());
      }
      StreamHasher hasher;
      std::vector<uint8_t> buf(CHUNK);
      while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        auto n = static_cast<size_t>(in.gcount());
        if (n > 0) {
          hasher.update(buf.data(), n);
        }
      }
      if (in.bad()) {
        throw std::runtime_error("Read error while hashing: " + fp.string());
      }
      co_return hasher.finalize();
    }(pool, path));
  } catch (const std::exception& e) {
    return {"", 0, std::string("Streaming hash failed: ") + e.what(), ErrorCode::StorageError};
  }
  return {std::move(id), fileSize, "", ErrorCode::Ok};
}

// ---------------------------------------------------------------------------
// hashOnlyBlob / hashOnlyFile
// ---------------------------------------------------------------------------

std::string Imager::hashOnlyBlob(const Blob& blob) {
  try {
    return computeSha256(blob);
  } catch (...) {
    return {};
  }
}

std::string Imager::hashOnlyFile(const std::filesystem::path& path, StageCallback onStage) {
  auto r = m_impl->readAndHash(path, std::move(onStage));
  return r.id;
}

// ---------------------------------------------------------------------------
// deleteBlob / deleteFile
// ---------------------------------------------------------------------------

DeleteResult Imager::deleteBlob(const Blob& blob) {
  std::string id;
  try {
    id = computeSha256(blob);
  } catch (const std::exception& e) {
    return {ErrorCode::StorageError, "", std::string("Hashing failed: ") + e.what()};
  }

  std::lock_guard<std::mutex> lock(m_impl->writeMutex);
  auto ec = m_impl->deleteByIdLocked(id);
  std::string msg;
  if (ec == ErrorCode::FileNotFound) {
    msg = "Not found: " + id;
  } else if (ec != ErrorCode::Ok) {
    msg = "Delete failed for: " + id;
  }
  return {ec, id, std::move(msg)};
}

DeleteResult Imager::deleteFile(const std::filesystem::path& path, StageCallback onStage) {
  auto r = m_impl->readAndHash(path, onStage);
  if (r.id.empty()) {
    return {r.errCode, "", r.error};
  }

  if (onStage) {
    onStage(ProcessingStage::WaitingMutex);
  }
  std::lock_guard<std::mutex> lock(m_impl->writeMutex);
  if (onStage) {
    onStage(ProcessingStage::DedupChecking);
  }

  auto ec = m_impl->deleteByIdLocked(r.id);
  std::string msg;
  if (ec == ErrorCode::FileNotFound) {
    msg = "Not found: " + r.id;
  } else if (ec != ErrorCode::Ok) {
    msg = "Delete failed for: " + r.id;
  }
  return {ec, r.id, std::move(msg)};
}

// ---------------------------------------------------------------------------
// Tag operations
// ---------------------------------------------------------------------------

ErrorCode Imager::tagImage(const std::string& id, const std::string& tag) {
  try {
    m_impl->dbs.bindTag(id, tag);
    return ErrorCode::Ok;
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::NotFound) {
      return ErrorCode::FileNotFound;
    }
    return ErrorCode::DatabaseError;
  }
}

ErrorCode Imager::untagImage(const std::string& id, const std::string& tag) {
  try {
    m_impl->dbs.unbindTag(id, tag);
    return ErrorCode::Ok;
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::NotFound) {
      return ErrorCode::FileNotFound;
    }
    return ErrorCode::DatabaseError;
  }
}

std::vector<std::string> Imager::getImageTags(const std::string& id) {
  try {
    return m_impl->dbs.getTagsForFile(id);
  } catch (const db::DatabaseException&) {
    return {};
  }
}

// ---------------------------------------------------------------------------
// getImageData
// ---------------------------------------------------------------------------

Blob Imager::getImageData(const std::string& id) {
  std::optional<db::File> file;
  try {
    file = m_impl->dbs.getFile(id);
  } catch (const db::DatabaseException&) {
    return {};
  }
  if (!file) {
    return {};
  }

  // For sidecar files, use storage_id (which may differ from file id)
  // to compute the correct disk path.
  std::string storageId = id;
  try {
    auto companion = m_impl->dbs.getCompanion(id);
    if (companion) {
      storageId = companion->storageId;
    }
  } catch (const db::DatabaseException&) {
    // Fall back to own id
  }

  return m_impl->storage.readFile(storageId, file->ext);
}

// ---------------------------------------------------------------------------
// System-level tag operations
// ---------------------------------------------------------------------------

ErrorCode Imager::createTag(const std::string& name) {
  try {
    m_impl->dbs.addTag(name);
    return ErrorCode::Ok;
  } catch (const db::DatabaseException&) {
    return ErrorCode::DatabaseError;
  }
}

ErrorCode Imager::deleteTag(const std::string& name) {
  try {
    m_impl->dbs.deleteTag(name);
    return ErrorCode::Ok;
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::NotFound) {
      return ErrorCode::FileNotFound;
    }
    return ErrorCode::DatabaseError;
  }
}

std::vector<std::string> Imager::listTags(uint32_t offset, uint32_t limit) {
  try {
    return m_impl->dbs.getAllTags(db::Pagination{offset, limit});
  } catch (const db::DatabaseException&) {
    return {};
  }
}

// ---------------------------------------------------------------------------
// List / count
// ---------------------------------------------------------------------------

std::vector<ImageInfo> Imager::listImages(uint32_t offset, uint32_t limit) {
  try {
    auto files = m_impl->dbs.getAllFiles(db::Pagination{offset, limit});
    return coro::blockOn(m_impl->pool, m_impl->enrichWithTags(std::move(files)));
  } catch (const db::DatabaseException&) {
    return {};
  }
}

uint64_t Imager::imageCount() {
  try {
    return m_impl->dbs.fileCount();
  } catch (const db::DatabaseException&) {
    return 0;
  }
}

const metrics::Metrics& Imager::metrics() const noexcept {
  return m_impl->metrics;
}

} // namespace imager
