#include "imager/Imager.h"

#include <metrics/Metrics.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "FileStorage.h"
#include "Hasher.h"
#include "MultiDatabase.h"
#include "Validators.h"
#include "coro/BlockOn.h"
#include "coro/ThreadPool.h"
#include "coro/WhenAll.h"
#include "imager/ImageValidator.h"

namespace imager {

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
  coro::ThreadPool pool;
  MultiDatabase dbs;
  FileStorage storage;
  std::vector<std::unique_ptr<validation::IValidator>> validators;
  std::mutex writeMutex;

  explicit Impl(const config::AppConfig& cfg)
    : pool(defaultPoolSize(cfg.targets.size())),
      dbs(cfg.targets, pool),
      storage(extractRoots(cfg.targets), pool),
      validators(createDefaultValidators()) {}

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
// addImage
// ---------------------------------------------------------------------------

AddResult Imager::addImage(const Blob& blob, const std::string& filename) {
  metrics::Timer total(metrics::Metrics::get().addimage_total);

  // 1. Split filename into (sourceDir, bareName) and extract extension
  auto [sourceDir, bareName] = Impl::splitFilename(filename);
  std::string ext = Impl::lowercaseExt(bareName);
  if (ext.empty()) {
    return {ErrorCode::UnsupportedFormat, "", "Filename has no extension"};
  }

  const auto* validator = m_impl->findValidator(ext);
  if (!validator && !Impl::isVideoExtension(ext)) {
    return {ErrorCode::UnsupportedFormat, "", "Unsupported format: " + ext};
  }

  // 2+3. Hash (and validate) — parallel when validator present
  std::string id;

  if (!validator) {
    try {
      metrics::Timer t(metrics::Metrics::get().hash);
      id = computeSha256(blob);
    } catch (const std::exception& e) {
      metrics::Metrics::get().images_failed.add(1);
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
              metrics::Timer t(metrics::Metrics::get().validate);
              out = v2->validate(b2.data(), b2.size());
            }(p, v, b, vRes)
          );

          tasks.push_back([](coro::ThreadPool& p2, Blob b2, std::string& out) -> coro::Task<void> {
            co_await p2.schedule();
            metrics::Timer t(metrics::Metrics::get().hash);
            out = computeSha256(b2);
          }(p, b, hId));

          co_await coro::whenAll(std::move(tasks));
          co_return std::make_pair(vRes, std::move(hId));
        }(m_impl->pool, validator, blob)
      );

      valResult = vr;
      id = std::move(hid);
    } catch (const std::exception& e) {
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::StorageError, "", std::string("Validation/hashing failed: ") + e.what()};
    }

    if (!valResult.valid) {
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::BrokenFile, "", valResult.errorMessage};
    }
  }

  // Measure mutex wait time separately from the lock guard
  {
    metrics::Timer t(metrics::Metrics::get().mutex_wait);
    m_impl->writeMutex.lock();
  }
  std::lock_guard<std::mutex> lock(m_impl->writeMutex, std::adopt_lock);

  // 4. Duplicate check
  try {
    metrics::Timer t(metrics::Metrics::get().dedup_check);
    if (m_impl->dbs.fileExists(id)) {
      return {ErrorCode::DuplicateFile, "", "File already exists: " + id};
    }
  } catch (const db::DatabaseException& e) {
    metrics::Metrics::get().images_failed.add(1);
    return {ErrorCode::DatabaseError, "", e.what()};
  }

  // ---- Sidecar path -------------------------------------------------------
  if (Impl::isSidecarExtension(ext)) {
    const std::string baseName = Impl::extractBaseName(bareName);

    // Look up parent candidate(s) via original_name table
    std::vector<db::File> parents;
    try {
      parents = m_impl->dbs.getFilesBySourceAndBaseName(sourceDir, baseName);
    } catch (const db::DatabaseException& e) {
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    // Filter out other sidecars from parent candidates
    std::vector<db::File> nonSidecarParents;
    for (const auto& p : parents) {
      if (!Impl::isSidecarExtension(p.ext)) {
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
        if (Impl::isImageExtension(p.ext)) {
          imageParents.push_back(p);
        }
      }
      if (imageParents.size() == 1) {
        storageId = imageParents[0].id;
        parentId = imageParents[0].id;
      } else {
        // Still ambiguous — reject
        metrics::Metrics::get().images_failed.add(1);
        return {ErrorCode::StorageError, "", "Ambiguous sidecar: multiple parent files match for '" + bareName + "'"};
      }
    }

    // 5. Write sidecar to storage using storageId as the filename prefix
    try {
      metrics::Timer t(metrics::Metrics::get().storage_write);
      coro::blockOn(m_impl->pool, m_impl->storage.writeFileAsync(storageId, ext, blob));
    } catch (const std::exception& e) {
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::StorageError, "", std::string("Storage write failed: ") + e.what()};
    }

    // 6. Insert file record (bare name only, not full path)
    try {
      metrics::Timer t(metrics::Metrics::get().db_insert);
      m_impl->dbs.addFile(id, bareName, blob.size(), ext);
    } catch (const db::DatabaseException& e) {
      if (e.code() == db::DatabaseErrorCode::ConstraintViolation) {
        // Clean up storage — already written with storageId
        coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(storageId, ext));
        return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
      }
      coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(storageId, ext));
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    // 7. Insert original_name entry
    try {
      m_impl->dbs.addOriginalName(sourceDir, baseName, id);
    } catch (const db::DatabaseException& e) {
      // Best effort rollback
      try {
        m_impl->dbs.deleteFile(id);
      } catch (...) {}
      coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(storageId, ext));
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    // 8. Insert file_companion entry
    try {
      m_impl->dbs.addCompanion(id, parentId, storageId);
    } catch (const db::DatabaseException& e) {
      try {
        m_impl->dbs.deleteFile(id);
      } catch (...) {}
      coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(storageId, ext));
      metrics::Metrics::get().images_failed.add(1);
      return {ErrorCode::DatabaseError, "", e.what()};
    }

    metrics::Metrics::get().images_added.add(1);
    return {ErrorCode::Ok, id, ""};
  }

  // ---- Non-sidecar path ---------------------------------------------------

  // 5. Write to all storage roots in parallel
  try {
    metrics::Timer t(metrics::Metrics::get().storage_write);
    coro::blockOn(m_impl->pool, m_impl->storage.writeFileAsync(id, ext, blob));
  } catch (const std::exception& e) {
    metrics::Metrics::get().images_failed.add(1);
    return {ErrorCode::StorageError, "", std::string("Storage write failed: ") + e.what()};
  }

  // 6. Insert into all databases in parallel (store bare name)
  try {
    metrics::Timer t(metrics::Metrics::get().db_insert);
    m_impl->dbs.addFile(id, bareName, blob.size(), ext);
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::ConstraintViolation) {
      return {ErrorCode::DuplicateFile, "", "Duplicate file: " + id};
    }
    // Roll back storage
    coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(id, ext));
    metrics::Metrics::get().images_failed.add(1);
    return {ErrorCode::DatabaseError, "", e.what()};
  }

  // 7. Insert original_name entry so sidecars can find this file as a parent
  const std::string baseName = Impl::extractBaseName(bareName);
  try {
    m_impl->dbs.addOriginalName(sourceDir, baseName, id);
  } catch (const db::DatabaseException&) {
    // Non-fatal: original_name uses INSERT OR IGNORE, but if something else
    // goes wrong, don't fail the whole addImage — the file is already stored.
  }

  // 8. Resolve orphan sidecars that were waiting for this parent
  try {
    auto orphans = m_impl->dbs.getOrphanCompanionsBySourceAndBaseName(sourceDir, baseName);
    for (const auto& orphan : orphans) {
      // Get the sidecar's extension from the file record
      auto sidecarFile = m_impl->dbs.getFile(orphan.fileId);
      if (!sidecarFile) {
        continue;
      }
      // Relocate sidecar file on disk: old storage path -> new storage path
      try {
        coro::blockOn(m_impl->pool, m_impl->storage.relocateFileAsync(orphan.storageId, id, sidecarFile->ext));
      } catch (const std::exception&) {
        continue; // Best-effort relocation; don't fail parent add
      }
      // Update companion record to point to this parent
      try {
        m_impl->dbs.updateCompanionParent(orphan.fileId, id, id);
      } catch (const db::DatabaseException&) {
        // Best-effort
      }
    }
  } catch (const db::DatabaseException&) {
    // Best-effort orphan resolution; don't fail parent add
  }

  metrics::Metrics::get().images_added.add(1);
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
// deleteImage — parallel storage cleanup after DB delete
// ---------------------------------------------------------------------------

ErrorCode Imager::deleteImage(const std::string& id) {
  std::lock_guard<std::mutex> lock(m_impl->writeMutex);

  std::optional<db::File> file;
  try {
    file = m_impl->dbs.getFile(id);
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
    companions = m_impl->dbs.getCompanionsForParent(id);
  } catch (const db::DatabaseException&) {
    // Best-effort; proceed with deletion
  }

  // Delete each sidecar's DB record (cascades file_companion, original_name)
  // and its storage file (using storage_id to find the correct disk path)
  for (const auto& comp : companions) {
    auto sidecarFile = m_impl->dbs.getFile(comp.fileId);
    if (sidecarFile) {
      try {
        m_impl->dbs.deleteFile(comp.fileId);
      } catch (const db::DatabaseException&) {
        // Best-effort
      }
      coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(comp.storageId, sidecarFile->ext));
    }
  }

  try {
    m_impl->dbs.deleteFile(id);
  } catch (const db::DatabaseException& e) {
    if (e.code() == db::DatabaseErrorCode::NotFound) {
      return ErrorCode::FileNotFound;
    }
    return ErrorCode::DatabaseError;
  }

  // Storage cleanup in parallel (best-effort)
  coro::blockOn(m_impl->pool, m_impl->storage.deleteFileAsync(id, ext));
  return ErrorCode::Ok;
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

} // namespace imager
