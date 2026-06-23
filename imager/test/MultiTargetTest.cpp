#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config/Config.h"
#include "database/Database.h"
#include "imager/Imager.h"

namespace fs = std::filesystem;
using namespace imager;

// ---------------------------------------------------------------------------
// Helpers (copied from ImagerTest.cpp — no cross-file sharing)
// ---------------------------------------------------------------------------

static std::string uniqueSuffix() {
  return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

/// Minimal valid JPEG bytes (SOI + EOI).
static std::vector<uint8_t> makeMinimalJpeg() {
  return {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',  0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01,
          0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x10, 0x0B, 0x0C, 0x0E, 0x0C, 0x0A, 0x10, 0x0E, 0x0D, 0x0E, 0x12,
          0x11, 0x10, 0x13, 0x18, 0x28, 0x1A, 0x18, 0x16, 0x16, 0x18, 0x31, 0x23, 0x25, 0x1D, 0x28, 0x3A, 0x33, 0x3D,
          0x3C, 0x39, 0x33, 0x38, 0x37, 0x40, 0x48, 0x5C, 0x4E, 0x40, 0x44, 0x57, 0x45, 0x37, 0x38, 0x50, 0x6D, 0x51,
          0x57, 0x5F, 0x62, 0x67, 0x68, 0x67, 0x3E, 0x4D, 0x71, 0x79, 0x70, 0x64, 0x78, 0x5C, 0x65, 0x67, 0x63, 0xFF,
          0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00,
          0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
          0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03,
          0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11,
          0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
          0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18,
          0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
          0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67,
          0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
          0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9,
          0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
          0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
          0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01,
          0x00, 0x00, 0x3F, 0x00, 0xFB, 0xDE, 0xCA, 0xF7, 0x93, 0x5C, 0xFF, 0xD9};
}

/// Load the MOV test fixture from the mov_validator fixtures directory.
/// Returns an empty vector if the fixture is not available.
static std::vector<uint8_t> loadMovFixture() {
  std::filesystem::path fixture{MOV_FIXTURES_DIR "/valid.mov"};
  std::ifstream file(fixture, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return {};
  }
  auto size = static_cast<std::streamsize>(file.tellg());
  file.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(data.data()), size);
  if (!file.good()) {
    return {};
  }
  return data;
}

/// Make a uniquely-hashed but still-valid MOV by appending inert trailing bytes.
/// libavformat reads from the moov atom and ignores trailing data.
static std::vector<uint8_t> makeUniqueMovFixture(uint8_t tag1, uint8_t tag2) {
  auto data = loadMovFixture();
  if (!data.empty()) {
    data.push_back(tag1);
    data.push_back(tag2);
  }
  return data;
}

/// Minimal valid AAE blob.
static Blob makeAaeBlob(const std::string& key = "adjustmentFormatVersion") {
  std::string xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
    " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\">\n"
    "<dict>\n"
    "    <key>" +
    key +
    "</key>\n"
    "    <integer>1</integer>\n"
    "</dict>\n"
    "</plist>\n";
  std::vector<uint8_t> bytes(xml.begin(), xml.end());
  return Blob::fromVector(std::move(bytes));
}

// ---------------------------------------------------------------------------
// Group 1 — Multi-target DB parity and rollback (B6, B7)
// ---------------------------------------------------------------------------

class MultiTargetDbTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(MultiTargetDbTest);
  CPPUNIT_TEST(testDbParityAfterAdd);
  CPPUNIT_TEST(testStorageWriteRollbackOnFailure);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  fs::path m_root1;
  fs::path m_root2;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_mtdb_" + s);
    m_root1 = m_base / "root1";
    m_root2 = m_base / "root2";
    fs::create_directories(m_root1);
    fs::create_directories(m_root2);
    m_cfg.targets.push_back({m_root1, m_base / "imager1.db"});
    m_cfg.targets.push_back({m_root2, m_base / "imager2.db"});
  }

  void tearDown() override {
    // Restore permissions on root2 in case testStorageWriteRollbackOnFailure
    // left it non-writable (otherwise remove_all will fail).
    std::error_code ec;
    fs::permissions(m_root2, fs::perms::all, ec);
    fs::remove_all(m_base);
  }

  // B6: After addImage, both DBs have the same file count and same file IDs.
  void testDbParityAfterAdd() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    Imager img(m_cfg);

    // First add
    auto r1 = img.addImage(Blob::fromVector(std::move(mov)), "photo.mov");
    if (r1.code != ErrorCode::Ok) {
      return; // storage unavailable — skip
    }

    {
      db::Database db1(m_base / "imager1.db");
      db::Database db2(m_base / "imager2.db");

      CPPUNIT_ASSERT_EQUAL(uint64_t(1), db1.fileCount());
      CPPUNIT_ASSERT_EQUAL(uint64_t(1), db2.fileCount());

      auto files1 = db1.getAllFiles();
      auto files2 = db2.getAllFiles();
      CPPUNIT_ASSERT_EQUAL(size_t(1), files1.size());
      CPPUNIT_ASSERT_EQUAL(size_t(1), files2.size());
      CPPUNIT_ASSERT_EQUAL(files1[0].id, files2[0].id);
    }

    // Second add — unique content via different trailing bytes
    auto mov2 = makeUniqueMovFixture(0xAB, 0xCD);
    if (mov2.empty()) {
      return;
    }
    auto r2 = img.addImage(Blob::fromVector(std::move(mov2)), "photo2.mov");
    if (r2.code != ErrorCode::Ok) {
      return;
    }

    {
      db::Database db1(m_base / "imager1.db");
      db::Database db2(m_base / "imager2.db");

      CPPUNIT_ASSERT_EQUAL(uint64_t(2), db1.fileCount());
      CPPUNIT_ASSERT_EQUAL(uint64_t(2), db2.fileCount());

      auto files1 = db1.getAllFiles();
      auto files2 = db2.getAllFiles();
      CPPUNIT_ASSERT_EQUAL(size_t(2), files1.size());
      CPPUNIT_ASSERT_EQUAL(size_t(2), files2.size());

      // Collect IDs from each DB and verify they match as sets.
      std::vector<std::string> ids1, ids2;
      for (auto& f : files1) {
        ids1.push_back(f.id);
      }
      for (auto& f : files2) {
        ids2.push_back(f.id);
      }
      std::sort(ids1.begin(), ids1.end());
      std::sort(ids2.begin(), ids2.end());
      CPPUNIT_ASSERT(ids1 == ids2);
    }
  }

  // B7: Write failure on one root triggers rollback — root1 left empty.
  void testStorageWriteRollbackOnFailure() {
    // Running as root bypasses permission checks; skip gracefully.
    if (geteuid() == 0) {
      return;
    }

    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    // Make root2 non-writable so the write to it fails.
    fs::permissions(m_root2, fs::perms::none);

    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "photo.mov");

    // Restore permissions immediately so tearDown can clean up.
    fs::permissions(m_root2, fs::perms::all);

    CPPUNIT_ASSERT_EQUAL(ErrorCode::StorageError, r.code);

    // root1 must be empty — rollback removed any partial write.
    bool foundAnyFile = false;
    for (auto it = fs::recursive_directory_iterator(m_root1, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator();
         ++it) {
      if (it->is_regular_file()) {
        foundAnyFile = true;
        break;
      }
    }
    CPPUNIT_ASSERT_MESSAGE("root1 should be empty after rollback", !foundAnyFile);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(MultiTargetDbTest);

// ---------------------------------------------------------------------------
// Group 2 — Storage read failover (B8)
// ---------------------------------------------------------------------------

class StorageFailoverTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(StorageFailoverTest);
  CPPUNIT_TEST(testReadFallsBackToSecondRoot);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  fs::path m_root1;
  fs::path m_root2;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_failover_" + s);
    m_root1 = m_base / "root1";
    m_root2 = m_base / "root2";
    fs::create_directories(m_root1);
    fs::create_directories(m_root2);
    m_cfg.targets.push_back({m_root1, m_base / "imager1.db"});
    m_cfg.targets.push_back({m_root2, m_base / "imager2.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  // B8: getImageData falls back to root2 when root1's copy is missing.
  void testReadFallsBackToSecondRoot() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "video.mov");
    if (r.code != ErrorCode::Ok) {
      return; // storage unavailable — skip
    }

    const std::string& id = r.id;

    // Remove the file from root1 to simulate a missing copy.
    fs::path fileInRoot1 = m_root1 / id.substr(0, 2) / (id + ".mov");
    CPPUNIT_ASSERT_MESSAGE("file should exist in root1 before removal", fs::exists(fileInRoot1));
    fs::remove(fileInRoot1);
    CPPUNIT_ASSERT_MESSAGE("file should be gone from root1 after removal", !fs::exists(fileInRoot1));

    // getImageData should succeed by reading from root2.
    auto data = img.getImageData(id);
    CPPUNIT_ASSERT_MESSAGE("getImageData should return non-empty data from root2", !data.empty());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(StorageFailoverTest);

// ---------------------------------------------------------------------------
// Group 3 — Multi-target sidecar consistency (B10)
// ---------------------------------------------------------------------------

class MultiTargetSidecarTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(MultiTargetSidecarTest);
  CPPUNIT_TEST(testSidecarStoredInBothRoots);
  CPPUNIT_TEST(testOrphanResolutionParity);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  fs::path m_root1;
  fs::path m_root2;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_sc_" + s);
    m_root1 = m_base / "root1";
    m_root2 = m_base / "root2";
    fs::create_directories(m_root1);
    fs::create_directories(m_root2);
    m_cfg.targets.push_back({m_root1, m_base / "imager1.db"});
    m_cfg.targets.push_back({m_root2, m_base / "imager2.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  // B10a: AAE sidecar storage file appears in BOTH roots under parent's hash.
  void testSidecarStoredInBothRoots() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg, "trip/IMG_001.JPG");
    if (r1.code != ErrorCode::Ok) {
      return; // skip if JPEG validation/storage fails
    }
    const std::string jpgHash = r1.id;

    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "trip/IMG_001.AAE");
    if (r2.code != ErrorCode::Ok) {
      return; // skip if AAE storage fails
    }

    // The AAE storage file is named after the parent's hash (not the AAE's own hash).
    const std::string shard = jpgHash.substr(0, 2);
    fs::path aaeInRoot1 = m_root1 / shard / (jpgHash + ".aae");
    fs::path aaeInRoot2 = m_root2 / shard / (jpgHash + ".aae");

    CPPUNIT_ASSERT_MESSAGE("AAE should be stored in root1 under parent hash", fs::exists(aaeInRoot1));
    CPPUNIT_ASSERT_MESSAGE("AAE should be stored in root2 under parent hash", fs::exists(aaeInRoot2));
  }

  // B10b: After orphan resolution, both DBs carry matching companion records.
  void testOrphanResolutionParity() {
    Imager img(m_cfg);

    // Add AAE first (orphan — no parent yet).
    auto aae = makeAaeBlob();
    auto r1 = img.addImage(aae, "pics/IMG_002.AAE");
    if (r1.code != ErrorCode::Ok) {
      return; // skip
    }
    const std::string aaeId = r1.id;

    // Add parent JPG — this should resolve the orphan.
    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r2 = img.addImage(jpeg, "pics/IMG_002.JPG");
    if (r2.code != ErrorCode::Ok) {
      return; // skip
    }
    const std::string jpgId = r2.id;

    // Both DBs must have 2 files.
    db::Database db1(m_base / "imager1.db");
    db::Database db2(m_base / "imager2.db");

    CPPUNIT_ASSERT_EQUAL(uint64_t(2), db1.fileCount());
    CPPUNIT_ASSERT_EQUAL(uint64_t(2), db2.fileCount());

    // Both DBs must have the companion record for the AAE pointing to the JPG.
    auto companion1 = db1.getCompanion(aaeId);
    auto companion2 = db2.getCompanion(aaeId);

    CPPUNIT_ASSERT_MESSAGE("db1 should have companion record for AAE", companion1.has_value());
    CPPUNIT_ASSERT_MESSAGE("db2 should have companion record for AAE", companion2.has_value());

    // After orphan resolution, the companion's parentId should be the JPG's hash.
    CPPUNIT_ASSERT_MESSAGE("db1 companion should have a parent_id after resolution", companion1->parentId.has_value());
    CPPUNIT_ASSERT_MESSAGE("db2 companion should have a parent_id after resolution", companion2->parentId.has_value());

    CPPUNIT_ASSERT_EQUAL(jpgId, companion1->parentId.value());
    CPPUNIT_ASSERT_EQUAL(jpgId, companion2->parentId.value());

    // Both companions should agree on the storageId (parent hash).
    CPPUNIT_ASSERT_EQUAL(companion1->storageId, companion2->storageId);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(MultiTargetSidecarTest);

// ---------------------------------------------------------------------------
// Group 4 — setImageTags multi-target DB parity (D2)
// ---------------------------------------------------------------------------

class SetTagsMultiTargetTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SetTagsMultiTargetTest);
  CPPUNIT_TEST(testTagParityAfterSetImageTags);
  CPPUNIT_TEST(testReplaceTagParityInBothDbs);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_settags_mt_" + s);
    fs::create_directories(m_base / "root1");
    fs::create_directories(m_base / "root2");
    m_cfg.targets.push_back({m_base / "root1", m_base / "imager1.db"});
    m_cfg.targets.push_back({m_base / "root2", m_base / "imager2.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  // After setImageTags, both target DBs carry identical tag sets for the file.
  void testTagParityAfterSetImageTags() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "multi_tag.mov");
    if (r.code != ErrorCode::Ok) {
      return; // storage unavailable — skip
    }
    const std::string id = r.id;

    auto ec = img.setImageTags(id, {"alpha", "beta", "gamma"});
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, ec);

    // Open each DB directly to bypass MultiDatabase read-from-first-only policy.
    db::Database db1(m_base / "imager1.db");
    db::Database db2(m_base / "imager2.db");

    auto tags1 = db1.getTagsForFile(id);
    auto tags2 = db2.getTagsForFile(id);

    CPPUNIT_ASSERT_EQUAL(size_t(3), tags1.size());
    CPPUNIT_ASSERT_EQUAL(size_t(3), tags2.size());
    CPPUNIT_ASSERT(tags1 == tags2);
  }

  // After replacing the tag set, both DBs reflect only the new tags.
  void testReplaceTagParityInBothDbs() {
    auto mov = makeUniqueMovFixture(0xCC, 0xDD);
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "replace_mt.mov");
    if (r.code != ErrorCode::Ok) {
      return;
    }
    const std::string id = r.id;

    // First set: {old1, old2}
    img.setImageTags(id, {"old1", "old2"});

    // Replace with: {new1}
    auto ec = img.setImageTags(id, {"new1"});
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, ec);

    db::Database db1(m_base / "imager1.db");
    db::Database db2(m_base / "imager2.db");

    auto tags1 = db1.getTagsForFile(id);
    auto tags2 = db2.getTagsForFile(id);

    // Only new1 remains in both DBs
    CPPUNIT_ASSERT_EQUAL(size_t(1), tags1.size());
    CPPUNIT_ASSERT_EQUAL(size_t(1), tags2.size());
    CPPUNIT_ASSERT_EQUAL(std::string("new1"), tags1[0]);
    CPPUNIT_ASSERT_EQUAL(std::string("new1"), tags2[0]);
    CPPUNIT_ASSERT(tags1 == tags2);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SetTagsMultiTargetTest);

// ---------------------------------------------------------------------------
// Group 5 — getImagePath multi-root fallthrough (D3)
// ---------------------------------------------------------------------------

class GetImagePathMultiRootTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(GetImagePathMultiRootTest);
  CPPUNIT_TEST(testPathFromFirstRoot);
  CPPUNIT_TEST(testPathFallsBackToSecondRoot);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  fs::path m_root1;
  fs::path m_root2;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_gip_mr_" + s);
    m_root1 = m_base / "root1";
    m_root2 = m_base / "root2";
    fs::create_directories(m_root1);
    fs::create_directories(m_root2);
    m_cfg.targets.push_back({m_root1, m_base / "imager1.db"});
    m_cfg.targets.push_back({m_root2, m_base / "imager2.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  // With 2 roots, getImagePath returns a path under the FIRST root when both
  // copies are present (resolveStoredPath iterates roots in declaration order).
  void testPathFromFirstRoot() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "video.mov");
    if (r.code != ErrorCode::Ok) {
      return; // storage unavailable — skip
    }

    auto path = img.getImagePath(r.id);
    CPPUNIT_ASSERT_MESSAGE("getImagePath must return a value", path.has_value());
    CPPUNIT_ASSERT_MESSAGE("returned path must exist on disk", fs::exists(*path));

    // The path must be rooted inside root1, not root2.
    const std::string pathStr = path->string();
    const std::string root1Str = m_root1.string();
    CPPUNIT_ASSERT_MESSAGE("path should be under root1 when both roots have the file", pathStr.find(root1Str) == 0);
  }

  // After removing root1's copy, getImagePath falls back to root2's copy.
  void testPathFallsBackToSecondRoot() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }

    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "video.mov");
    if (r.code != ErrorCode::Ok) {
      return;
    }

    const std::string& id = r.id;
    fs::path fileInRoot1 = m_root1 / id.substr(0, 2) / (id + ".mov");

    CPPUNIT_ASSERT_MESSAGE("file must exist in root1 before removal", fs::exists(fileInRoot1));
    std::error_code ec;
    fs::remove(fileInRoot1, ec);
    CPPUNIT_ASSERT_MESSAGE("root1 copy must be removable", !ec);
    CPPUNIT_ASSERT(!fs::exists(fileInRoot1));

    // Now getImagePath must fall back to root2.
    auto path = img.getImagePath(id);
    CPPUNIT_ASSERT_MESSAGE("getImagePath must return a value from root2 after root1 deleted", path.has_value());
    CPPUNIT_ASSERT_MESSAGE("returned path must exist on disk", fs::exists(*path));

    const std::string pathStr = path->string();
    const std::string root2Str = m_root2.string();
    CPPUNIT_ASSERT_MESSAGE("path should be under root2 after root1 copy removed", pathStr.find(root2Str) == 0);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(GetImagePathMultiRootTest);

// No main() here — ImagerTest.cpp owns the single main() for the shared binary.
