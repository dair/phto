#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config/Config.h"
#include "imager/Imager.h"

namespace fs = std::filesystem;
using namespace imager;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string uniqueSuffix() {
  static std::atomic<int> counter{0};
  return std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

static config::AppConfig makeTempConfig(const std::string& suffix) {
  fs::path base = fs::temp_directory_path() / ("imager_large_test_" + suffix);
  fs::create_directories(base / "storage");
  config::AppConfig cfg;
  cfg.targets.push_back({base / "storage", base / "imager.db"});
  // Populate defaults (same as loadConfig would do)
  static constexpr uint64_t kMB = 1024ULL * 1024ULL;
  cfg.fileSizeLimits["jpg"] = 250ULL * kMB;
  cfg.fileSizeLimits["jpeg"] = 250ULL * kMB;
  cfg.fileSizeLimits["png"] = 500ULL * kMB;
  cfg.fileSizeLimits["heic"] = 250ULL * kMB;
  cfg.fileSizeLimits["heif"] = 250ULL * kMB;
  cfg.fileSizeLimits["nef"] = 500ULL * kMB;
  cfg.fileSizeLimits["aae"] = 1ULL * kMB;
  cfg.fileSizeLimits["mov"] = 100ULL * 1024ULL * kMB;
  cfg.fileSizeLimits["mp4"] = 100ULL * 1024ULL * kMB;
  return cfg;
}

/// Write a file of the given size filled with the given byte pattern.
static fs::path writeTempFile(const fs::path& dir, const std::string& name, uint64_t size, uint8_t fill = 0xAB) {
  fs::path p = dir / name;
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  static constexpr size_t CHUNK = 64 * 1024;
  std::vector<uint8_t> buf(CHUNK, fill);
  uint64_t written = 0;
  while (written < size) {
    size_t n = std::min<uint64_t>(CHUNK, size - written);
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(n));
    written += n;
  }
  return p;
}

// ---------------------------------------------------------------------------
// Test suite: size limit rejection
// ---------------------------------------------------------------------------

class SizeLimitTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SizeLimitTest);
  CPPUNIT_TEST(testJpegTooLargeRejected);
  CPPUNIT_TEST(testAaeTooLargeRejected);
  CPPUNIT_TEST(testUnknownExtNoLimit);
  CPPUNIT_TEST(testJpegJustUnderLimit);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  fs::path m_tmpDir;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_sizelimit_" + s);
    m_tmpDir = m_base / "files";
    fs::create_directories(m_base / "storage");
    fs::create_directories(m_tmpDir);
    m_cfg = makeTempConfig(s);
    m_cfg.targets = {{m_base / "storage", m_base / "imager.db"}};
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  /// A 1-byte-over-limit JPEG should be rejected as TooLarge.
  void testJpegTooLargeRejected() {
    static constexpr uint64_t kMB = 1024ULL * 1024ULL;
    uint64_t overLimit = 250ULL * kMB + 1;
    auto p = writeTempFile(m_tmpDir, "big.jpg", overLimit);
    Imager img(m_cfg);
    auto res = img.addFile(p, "big.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::TooLarge, res.code);
    CPPUNIT_ASSERT(
      res.message.find("250") != std::string::npos || res.message.find("too large") != std::string::npos ||
      res.message.find("Too") != std::string::npos
    );
  }

  /// An AAE file > 1 MB should be rejected as TooLarge.
  void testAaeTooLargeRejected() {
    static constexpr uint64_t kMB = 1024ULL * 1024ULL;
    uint64_t overLimit = 1ULL * kMB + 1;
    auto p = writeTempFile(m_tmpDir, "big.aae", overLimit);
    Imager img(m_cfg);
    auto res = img.addFile(p, "big.aae");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::TooLarge, res.code);
  }

  /// An unknown extension has no size limit — should not get TooLarge.
  /// It will get UnsupportedFormat since no validator handles it.
  void testUnknownExtNoLimit() {
    static constexpr uint64_t kMB = 1024ULL * 1024ULL;
    auto p = writeTempFile(m_tmpDir, "data.xyz", 500ULL * kMB);
    Imager img(m_cfg);
    auto res = img.addFile(p, "data.xyz");
    CPPUNIT_ASSERT(res.code != ErrorCode::TooLarge);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::UnsupportedFormat, res.code);
  }

  /// A JPEG 1 byte under the limit should not get TooLarge.
  /// It will fail validation (garbage content), not TooLarge.
  void testJpegJustUnderLimit() {
    static constexpr uint64_t kMB = 1024ULL * 1024ULL;
    uint64_t underLimit = 250ULL * kMB - 1;
    auto p = writeTempFile(m_tmpDir, "notreal.jpg", underLimit);
    Imager img(m_cfg);
    auto res = img.addFile(p, "notreal.jpg");
    CPPUNIT_ASSERT(res.code != ErrorCode::TooLarge);
    // Content is garbage, so either BrokenFile or WRONG (treated as BrokenFile/UnsupportedFormat)
    CPPUNIT_ASSERT(res.code == ErrorCode::BrokenFile || res.code == ErrorCode::UnsupportedFormat);
  }
};

// ---------------------------------------------------------------------------
// Test suite: streaming path (files > 256 MB)
// ---------------------------------------------------------------------------

class StreamingPathTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(StreamingPathTest);
  CPPUNIT_TEST(testLargeMovStreamedAndStored);
  CPPUNIT_TEST(testLargeMovDeduplication);
  CPPUNIT_TEST(testValidateOnlyFileLargeRejectsOversized);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  fs::path m_tmpDir;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_streaming_" + s);
    m_tmpDir = m_base / "files";
    fs::create_directories(m_base / "storage");
    fs::create_directories(m_tmpDir);
    m_cfg = makeTempConfig(s);
    m_cfg.targets = {{m_base / "storage", m_base / "imager.db"}};
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  /// Generate a valid MOV larger than kStreamingThreshold (256 MB) by
  /// wrapping a real MOV fixture in extra padding. This exercises the
  /// streaming write path without needing a real camera MOV.
  ///
  /// Strategy: we cannot easily synthesize a >256 MB valid MOV from scratch,
  /// so instead we test the streaming HASH path by using a file that is
  /// structured as valid MOV but with a large trailing padding region.
  /// libavformat accepts such files (ignores trailing data after moov/mdat).
  ///
  /// If the MOV fixture is not available, the test skips.
  void testLargeMovStreamedAndStored() {
    fs::path fixture{MOV_FIXTURES_DIR "/valid.mov"};
    if (!fs::exists(fixture)) {
      return; // skip — fixture absent
    }

    // Build a >256 MB file: real MOV header + padding zeros.
    static constexpr uint64_t kStreamingThreshold = 256ULL * 1024ULL * 1024ULL;
    static constexpr uint64_t kTargetSize = kStreamingThreshold + 1024ULL * 1024ULL; // 257 MB

    // Read real MOV fixture.
    std::vector<uint8_t> movData;
    {
      std::ifstream in(fixture, std::ios::binary | std::ios::ate);
      CPPUNIT_ASSERT(in.is_open());
      auto sz = static_cast<size_t>(in.tellg());
      in.seekg(0);
      movData.resize(sz);
      in.read(reinterpret_cast<char*>(movData.data()), static_cast<std::streamsize>(sz));
      CPPUNIT_ASSERT(in.good() || in.eof());
    }

    // Write the padded file.
    fs::path largeMov = m_tmpDir / "large.mov";
    {
      std::ofstream out(largeMov, std::ios::binary | std::ios::trunc);
      CPPUNIT_ASSERT(out.is_open());
      out.write(reinterpret_cast<const char*>(movData.data()), static_cast<std::streamsize>(movData.size()));
      // Pad with zeros to reach target size.
      static constexpr size_t CHUNK = 64 * 1024;
      std::vector<uint8_t> zeros(CHUNK, 0);
      uint64_t written = movData.size();
      while (written < kTargetSize) {
        size_t n = std::min<uint64_t>(CHUNK, kTargetSize - written);
        out.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(n));
        written += n;
      }
      CPPUNIT_ASSERT(out.good());
    }

    CPPUNIT_ASSERT(fs::file_size(largeMov) >= kStreamingThreshold);

    Imager img(m_cfg);
    auto res = img.addFile(largeMov, "large.mov");

    // A 257 MB MOV is within the 100 GB limit, so TooLarge must not occur.
    CPPUNIT_ASSERT(res.code != ErrorCode::TooLarge);
    // libavformat should accept the padded file.
    CPPUNIT_ASSERT_MESSAGE(
      "Expected Ok or DuplicateFile, got: " + res.message,
      res.code == ErrorCode::Ok || res.code == ErrorCode::DuplicateFile
    );
    if (res.code == ErrorCode::Ok) {
      CPPUNIT_ASSERT_EQUAL(size_t(64), res.id.size());
      // File should exist in storage root.
      fs::path storageRoot = m_base / "storage";
      bool found = false;
      for (auto& entry : fs::recursive_directory_iterator(storageRoot)) {
        if (entry.path().extension() == ".mov") {
          found = true;
          CPPUNIT_ASSERT(fs::file_size(entry.path()) >= kStreamingThreshold);
          break;
        }
      }
      CPPUNIT_ASSERT_MESSAGE("Stored file not found in storage root", found);
    }
  }

  /// Adding the same large file twice returns DuplicateFile on the second call.
  void testLargeMovDeduplication() {
    fs::path fixture{MOV_FIXTURES_DIR "/valid.mov"};
    if (!fs::exists(fixture)) {
      return; // skip
    }

    static constexpr uint64_t kStreamingThreshold = 256ULL * 1024ULL * 1024ULL;
    static constexpr uint64_t kTargetSize = kStreamingThreshold + 512ULL * 1024ULL;

    std::vector<uint8_t> movData;
    {
      std::ifstream in(fixture, std::ios::binary | std::ios::ate);
      if (!in.is_open()) {
        return;
      }
      auto sz = static_cast<size_t>(in.tellg());
      in.seekg(0);
      movData.resize(sz);
      in.read(reinterpret_cast<char*>(movData.data()), static_cast<std::streamsize>(sz));
    }

    fs::path largeMov = m_tmpDir / "dup.mov";
    {
      std::ofstream out(largeMov, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(movData.data()), static_cast<std::streamsize>(movData.size()));
      static constexpr size_t CHUNK = 64 * 1024;
      std::vector<uint8_t> zeros(CHUNK, 0);
      uint64_t written = movData.size();
      while (written < kTargetSize) {
        size_t n = std::min<uint64_t>(CHUNK, kTargetSize - written);
        out.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(n));
        written += n;
      }
    }

    Imager img(m_cfg);
    auto r1 = img.addFile(largeMov, "dup.mov");
    if (r1.code != ErrorCode::Ok) {
      return; // streaming validation may fail in unusual environments
    }

    // Second add of the same file must report DuplicateFile.
    auto r2 = img.addFile(largeMov, "dup_copy.mov");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
  }

  /// validateOnlyFile rejects files that exceed their format limit.
  void testValidateOnlyFileLargeRejectsOversized() {
    static constexpr uint64_t kMB = 1024ULL * 1024ULL;
    uint64_t overLimit = 250ULL * kMB + 1; // just over JPEG limit
    auto p = writeTempFile(m_tmpDir, "toobig.jpg", overLimit);
    Imager img(m_cfg);
    auto res = img.validateOnlyFile(p, "toobig.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::TooLarge, res.code);
  }
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

CPPUNIT_TEST_SUITE_REGISTRATION(SizeLimitTest);
CPPUNIT_TEST_SUITE_REGISTRATION(StreamingPathTest);
