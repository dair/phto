#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TextTestRunner.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "config/Config.h"
#include "imager/FileTimestamp.h"
#include "imager/Imager.h"

namespace fs = std::filesystem;
using namespace imager;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string uniqueSuffix() {
  static std::atomic<int> counter{0};
  // Include PID so re-runs do not reuse stale temp dirs from a prior invocation.
  return std::to_string(::getpid()) + "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

static config::AppConfig makeTempConfig(const std::string& suffix) {
  fs::path base = fs::temp_directory_path() / ("imager_ts_test_" + suffix);
  fs::create_directories(base / "storage");
  config::AppConfig cfg;
  cfg.targets.push_back({base / "storage", base / "imager.db"});
  return cfg;
}

/// Build a two-target config sharing the same database directory structure.
static config::AppConfig makeTwoRootConfig(const std::string& suffix) {
  fs::path base = fs::temp_directory_path() / ("imager_ts2_test_" + suffix);
  fs::create_directories(base / "storage0");
  fs::create_directories(base / "storage1");
  config::AppConfig cfg;
  cfg.targets.push_back({base / "storage0", base / "imager0.db"});
  cfg.targets.push_back({base / "storage1", base / "imager1.db"});
  return cfg;
}

/// Minimal valid JPEG bytes (SOI + quantization table + SOF + Huffman + SOS + EOI).
// clang-format off
static std::vector<uint8_t> makeMinimalJpeg() {
  return {
    0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',  0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x10, 0x0B, 0x0C, 0x0E, 0x0C, 0x0A, 0x10,
    0x0E, 0x0D, 0x0E, 0x12, 0x11, 0x10, 0x13, 0x18, 0x28, 0x1A, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23,
    0x25, 0x1D, 0x28, 0x3A, 0x33, 0x3D, 0x3C, 0x39, 0x33, 0x38, 0x37, 0x40, 0x48, 0x5C, 0x4E, 0x40,
    0x44, 0x57, 0x45, 0x37, 0x38, 0x50, 0x6D, 0x51, 0x57, 0x5F, 0x62, 0x67, 0x68, 0x67, 0x3E, 0x4D,
    0x71, 0x79, 0x70, 0x64, 0x78, 0x5C, 0x65, 0x67, 0x63, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
    0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03,
    0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00,
    0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
    0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55,
    0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2,
    0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
    0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA,
    0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xFB, 0xDE, 0xCA, 0xF7, 0x93, 0x5C, 0xFF, 0xD9,
  };
}

// clang-format on

/// Write bytes to a temp file and return its path.
static fs::path writeTempFile(const fs::path& dir, const std::string& name, const std::vector<uint8_t>& bytes) {
  fs::path p = dir / name;
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return p;
}

/// Set mtime and atime on a file to a fixed known time (2020-01-01 00:00:00 UTC, nsec=0).
static void setKnownTimestamps(const fs::path& p, time_t sec = 1577836800, long nsec = 0) {
  struct timespec times[2];
  times[0].tv_sec = sec;
  times[0].tv_nsec = nsec;
  times[1].tv_sec = sec;
  times[1].tv_nsec = nsec;
  if (::utimensat(AT_FDCWD, p.c_str(), times, 0) != 0) {
    throw std::system_error(errno, std::system_category(), "setKnownTimestamps failed: " + p.string());
  }
}

/// Stat a file and return {atime, mtime} as struct timespec pairs.
static std::pair<struct timespec, struct timespec> getTimestamps(const fs::path& p) {
  struct stat st{};
  if (::stat(p.c_str(), &st) != 0) {
    throw std::system_error(errno, std::system_category(), "stat failed: " + p.string());
  }
#ifdef __linux__
  return {st.st_atim, st.st_mtim};
#else
  return {st.st_atimespec, st.st_mtimespec};
#endif
}

/// Find the stored file under a storage root for the given id and extension.
static fs::path storedFilePath(const fs::path& root, const std::string& id, const std::string& ext) {
  std::string extNoDot = (ext.size() > 1 && ext[0] == '.') ? ext.substr(1) : ext;
  return root / id.substr(0, 2) / (id + "." + extNoDot);
}

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

class TimestampPreservationTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TimestampPreservationTest);
  CPPUNIT_TEST(testCopyTimestampsDirectly);
  CPPUNIT_TEST(testCopyTimestampsNanoseconds);
  CPPUNIT_TEST(testCopyTimestampsSourceMissing);
  CPPUNIT_TEST(testMtimePreservedOnAddFile);
  CPPUNIT_TEST(testAtimePreservedOnAddFile);
  CPPUNIT_TEST(testNanosecondPrecisionOnAddFile);
  CPPUNIT_TEST(testMultiRootBothCopiesPreserveTimestamp);
  CPPUNIT_TEST(testBlobIngestDoesNotRequireTimestamp);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() override {
    m_suffix = uniqueSuffix();
    m_tmpDir = fs::temp_directory_path() / ("imager_ts_direct_" + m_suffix);
    fs::create_directories(m_tmpDir);
  }

  void tearDown() override {
    std::error_code ec;
    fs::remove_all(m_tmpDir, ec);
  }

  // -------------------------------------------------------------------------
  // Direct copyTimestamps tests — no Imager stack
  // -------------------------------------------------------------------------

  /// copyTimestamps copies mtime and atime from source to dest.
  void testCopyTimestampsDirectly() {
    auto src = writeTempFile(m_tmpDir, "src.bin", {0x01, 0x02, 0x03});
    auto dst = writeTempFile(m_tmpDir, "dst.bin", {0x01, 0x02, 0x03});

    static constexpr time_t kKnownSec = 1577836800; // 2020-01-01 00:00:00 UTC
    setKnownTimestamps(src, kKnownSec, 0);

    copyTimestamps(src, dst);

    auto [dstAtime, dstMtime] = getTimestamps(dst);
    CPPUNIT_ASSERT_EQUAL(kKnownSec, dstMtime.tv_sec);
    CPPUNIT_ASSERT_EQUAL(kKnownSec, dstAtime.tv_sec);
  }

  /// copyTimestamps preserves sub-second nanoseconds.
  void testCopyTimestampsNanoseconds() {
    auto src = writeTempFile(m_tmpDir, "src_ns.bin", {0xAA, 0xBB});
    auto dst = writeTempFile(m_tmpDir, "dst_ns.bin", {0xAA, 0xBB});

    static constexpr time_t kSec = 1577836800;
    static constexpr long kNsec = 123456789L;
    setKnownTimestamps(src, kSec, kNsec);

    copyTimestamps(src, dst);

    auto [dstAtime, dstMtime] = getTimestamps(dst);
    CPPUNIT_ASSERT_EQUAL(kSec, dstMtime.tv_sec);
    // Nanosecond precision depends on the filesystem; only assert if the FS
    // stored non-zero nsec in the source (i.e., it supports sub-second).
    auto [srcAtime, srcMtime] = getTimestamps(src);
    if (srcMtime.tv_nsec != 0) {
      CPPUNIT_ASSERT_EQUAL(srcMtime.tv_nsec, dstMtime.tv_nsec);
      CPPUNIT_ASSERT_EQUAL(srcAtime.tv_nsec, dstAtime.tv_nsec);
    }
  }

  /// copyTimestamps throws std::system_error when the source path does not exist.
  void testCopyTimestampsSourceMissing() {
    auto dst = writeTempFile(m_tmpDir, "dst_miss.bin", {0x00});
    fs::path missing = m_tmpDir / "no_such_file.bin";

    bool threw = false;
    try {
      copyTimestamps(missing, dst);
    } catch (const std::system_error&) {
      threw = true;
    }
    CPPUNIT_ASSERT_MESSAGE("Expected std::system_error for missing source", threw);
  }

  // -------------------------------------------------------------------------
  // Imager::addFile timestamp tests
  // -------------------------------------------------------------------------

  /// mtime of the stored file matches the source file's mtime after addFile.
  void testMtimePreservedOnAddFile() {
    auto cfg = makeTempConfig(m_suffix + "a");
    auto& root = cfg.targets[0].root;

    fs::path srcDir = fs::temp_directory_path() / ("imager_ts_src_" + m_suffix + "a");
    fs::create_directories(srcDir);

    auto jpeg = makeMinimalJpeg();
    auto srcPath = writeTempFile(srcDir, "photo.jpg", jpeg);

    static constexpr time_t kKnown = 1577836800; // 2020-01-01 00:00:00 UTC
    setKnownTimestamps(srcPath, kKnown, 0);

    Imager imager(cfg);
    auto result = imager.addFile(srcPath, "photo.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile should succeed", ErrorCode::Ok, result.code);

    auto stored = storedFilePath(root, result.id, ".jpg");
    CPPUNIT_ASSERT_MESSAGE("stored file must exist", fs::exists(stored));

    auto [storedAtimeA, storedMtimeA] = getTimestamps(stored);
    (void)storedAtimeA;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("stored mtime must match source", kKnown, storedMtimeA.tv_sec);

    std::error_code ec;
    fs::remove_all(srcDir, ec);
    fs::remove_all(fs::temp_directory_path() / ("imager_ts_test_" + m_suffix + "a"), ec);
  }

  /// atime of the stored file matches the source file's atime after addFile.
  void testAtimePreservedOnAddFile() {
    auto cfg = makeTempConfig(m_suffix + "b");
    auto& root = cfg.targets[0].root;

    fs::path srcDir = fs::temp_directory_path() / ("imager_ts_src_" + m_suffix + "b");
    fs::create_directories(srcDir);

    auto jpeg = makeMinimalJpeg();
    auto srcPath = writeTempFile(srcDir, "photo2.jpg", jpeg);

    static constexpr time_t kKnown = 1609459200; // 2021-01-01 00:00:00 UTC
    setKnownTimestamps(srcPath, kKnown, 0);

    Imager imager(cfg);
    auto result = imager.addFile(srcPath, "photo2.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile should succeed", ErrorCode::Ok, result.code);

    auto stored = storedFilePath(root, result.id, ".jpg");
    auto [storedAtime, storedMtime] = getTimestamps(stored);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("stored atime must match source", kKnown, storedAtime.tv_sec);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("stored mtime must match source", kKnown, storedMtime.tv_sec);

    std::error_code ec;
    fs::remove_all(srcDir, ec);
    fs::remove_all(fs::temp_directory_path() / ("imager_ts_test_" + m_suffix + "b"), ec);
  }

  /// Sub-second mtime precision is preserved when the filesystem supports it.
  void testNanosecondPrecisionOnAddFile() {
    auto cfg = makeTempConfig(m_suffix + "c");
    auto& root = cfg.targets[0].root;

    fs::path srcDir = fs::temp_directory_path() / ("imager_ts_src_" + m_suffix + "c");
    fs::create_directories(srcDir);

    auto jpeg = makeMinimalJpeg();
    auto srcPath = writeTempFile(srcDir, "photo3.jpg", jpeg);

    static constexpr time_t kSec = 1577836800;
    static constexpr long kNsec = 987654321L;
    setKnownTimestamps(srcPath, kSec, kNsec);

    // Verify the source FS actually stored the nsec (skip assertion if not).
    auto [srcAtime, srcMtime] = getTimestamps(srcPath);

    Imager imager(cfg);
    auto result = imager.addFile(srcPath, "photo3.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile should succeed", ErrorCode::Ok, result.code);

    auto stored = storedFilePath(root, result.id, ".jpg");
    auto [storedAtimeC, storedMtimeC] = getTimestamps(stored);
    (void)storedAtimeC;

    CPPUNIT_ASSERT_EQUAL_MESSAGE("stored mtime seconds must match", kSec, storedMtimeC.tv_sec);
    if (srcMtime.tv_nsec != 0) {
      CPPUNIT_ASSERT_EQUAL_MESSAGE("stored mtime nanoseconds must match", srcMtime.tv_nsec, storedMtimeC.tv_nsec);
    }

    std::error_code ec;
    fs::remove_all(srcDir, ec);
    fs::remove_all(fs::temp_directory_path() / ("imager_ts_test_" + m_suffix + "c"), ec);
  }

  /// With two storage roots, both stored copies carry the correct mtime.
  void testMultiRootBothCopiesPreserveTimestamp() {
    auto cfg = makeTwoRootConfig(m_suffix + "d");

    fs::path srcDir = fs::temp_directory_path() / ("imager_ts_src_" + m_suffix + "d");
    fs::create_directories(srcDir);

    auto jpeg = makeMinimalJpeg();
    auto srcPath = writeTempFile(srcDir, "multi.jpg", jpeg);

    static constexpr time_t kKnown = 1640995200; // 2022-01-01 00:00:00 UTC
    setKnownTimestamps(srcPath, kKnown, 0);

    Imager imager(cfg);
    auto result = imager.addFile(srcPath, "multi.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile should succeed", ErrorCode::Ok, result.code);

    for (size_t i = 0; i < cfg.targets.size(); ++i) {
      auto stored = storedFilePath(cfg.targets[i].root, result.id, ".jpg");
      CPPUNIT_ASSERT_MESSAGE("stored file must exist on root " + std::to_string(i), fs::exists(stored));
      auto [storedAtimeD, storedMtimeD] = getTimestamps(stored);
      (void)storedAtimeD;
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "stored mtime must match source on root " + std::to_string(i), kKnown, storedMtimeD.tv_sec
      );
    }

    std::error_code ec;
    fs::remove_all(srcDir, ec);
    fs::remove_all(fs::temp_directory_path() / ("imager_ts2_test_" + m_suffix + "d"), ec);
  }

  /// addImage (blob path) succeeds without touching timestamps — no regression.
  void testBlobIngestDoesNotRequireTimestamp() {
    auto cfg = makeTempConfig(m_suffix + "e");

    auto jpeg = makeMinimalJpeg();
    Blob blob(jpeg.size());
    std::copy(jpeg.begin(), jpeg.end(), blob.writableData());
    blob.freeze();

    Imager imager(cfg);
    auto result = imager.addImage(blob, "blobphoto.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addImage should succeed", ErrorCode::Ok, result.code);
    CPPUNIT_ASSERT_MESSAGE("id must not be empty", !result.id.empty());

    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / ("imager_ts_test_" + m_suffix + "e"), ec);
  }

private:
  std::string m_suffix;
  fs::path m_tmpDir;
};

CPPUNIT_TEST_SUITE_REGISTRATION(TimestampPreservationTest);

int main() {
  CppUnit::TextTestRunner runner;
  CppUnit::TestFactoryRegistry& registry = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(registry.makeTest());
  return runner.run() ? 0 : 1;
}
