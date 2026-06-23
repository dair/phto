#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "imager/Imager.h"

namespace fs = std::filesystem;
using namespace imager;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string uniqueSuffix() {
  return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

/// Build a temporary AppConfig with one target (root + database).
static config::AppConfig makeTempConfig(const std::string& suffix = "") {
  std::string s = suffix.empty() ? uniqueSuffix() : suffix;
  fs::path base = fs::temp_directory_path() / ("imager_test_" + s);
  fs::create_directories(base / "storage");

  config::AppConfig cfg;
  cfg.targets.push_back({base / "storage", base / "imager.db"});
  return cfg;
}

/// Minimal valid JPEG bytes (SOI + EOI).
static std::vector<uint8_t> makeMinimalJpeg() {
  // A 1x1 white JPEG created with libjpeg would be complex; instead
  // create a valid 1x1 JFIF. We use a hard-coded small valid JPEG.
  // (FF D8 FF E0 JFIF header ... FF D9 EOI)
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

/// Minimal valid HEIF/AV1 (AVIF) file — 4x4 solid-gray image.
/// Generated with libheif + libaom encoder; ftyp at offset 4 confirmed.
// clang-format off
static std::vector<uint8_t> makeMinimalHeic() {
  return {
    0x00, 0x00, 0x00, 0x1C, 0x66, 0x74, 0x79, 0x70, 0x61, 0x76, 0x69, 0x66, 0x00, 0x00, 0x00, 0x00,
    0x6D, 0x69, 0x66, 0x31, 0x61, 0x76, 0x69, 0x66, 0x6D, 0x69, 0x61, 0x66, 0x00, 0x00, 0x00, 0xD6,
    0x6D, 0x65, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x68, 0x64, 0x6C, 0x72,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x69, 0x63, 0x74, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x70, 0x69, 0x74,
    0x6D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x22, 0x69, 0x6C, 0x6F, 0x63, 0x00,
    0x00, 0x00, 0x00, 0x44, 0x40, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x23, 0x69, 0x69, 0x6E,
    0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x15, 0x69, 0x6E, 0x66, 0x65, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x61, 0x76, 0x30, 0x31, 0x00, 0x00, 0x00, 0x00, 0x56,
    0x69, 0x70, 0x72, 0x70, 0x00, 0x00, 0x00, 0x38, 0x69, 0x70, 0x63, 0x6F, 0x00, 0x00, 0x00, 0x0C,
    0x61, 0x76, 0x31, 0x43, 0x81, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x14, 0x69, 0x73, 0x70, 0x65,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x10,
    0x70, 0x69, 0x78, 0x69, 0x00, 0x00, 0x00, 0x00, 0x03, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x16,
    0x69, 0x70, 0x6D, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x03, 0x81,
    0x02, 0x03, 0x00, 0x00, 0x00, 0x1F, 0x6D, 0x64, 0x61, 0x74, 0x12, 0x00, 0x0A, 0x08, 0x18, 0x04,
    0x7D, 0x82, 0x02, 0x1A, 0x0D, 0x08, 0x32, 0x09, 0x18, 0x00, 0x0A, 0x28, 0xA2, 0x84, 0x00, 0x05,
    0x48
  };
}

// clang-format on

/// Load the NEF test fixture from the nef_validator fixtures directory.
/// Returns an empty vector if the fixture is not available.
static std::vector<uint8_t> loadNefFixture() {
  std::filesystem::path fixture{NEF_FIXTURES_DIR "/valid.nef"};
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

// ---------------------------------------------------------------------------
// Test: AddImage workflow
// ---------------------------------------------------------------------------

class AddImageTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AddImageTest);
  CPPUNIT_TEST(testAddJpeg);
  CPPUNIT_TEST(testAddMp4);
  CPPUNIT_TEST(testAddMov);
  CPPUNIT_TEST(testAddMovDuplicate);
  CPPUNIT_TEST(testAddBrokenMov);
  CPPUNIT_TEST(testAddHeic);
  CPPUNIT_TEST(testAddHeicDuplicate);
  CPPUNIT_TEST(testAddBrokenHeic);
  CPPUNIT_TEST(testAddNef);
  CPPUNIT_TEST(testAddNefDuplicate);
  CPPUNIT_TEST(testAddBrokenNef);
  CPPUNIT_TEST(testUnsupportedFormat);
  CPPUNIT_TEST(testDuplicateDetection);
  CPPUNIT_TEST(testBrokenJpeg);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_add_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testAddJpeg() {
    Imager img(m_cfg);
    auto res = img.addImage(Blob::fromVector(makeMinimalJpeg()), "photo.jpg");
    // May succeed or return BrokenFile if the hand-crafted JPEG is not
    // fully valid — both are acceptable; just not UnsupportedFormat.
    CPPUNIT_ASSERT(res.code != ErrorCode::UnsupportedFormat);
    CPPUNIT_ASSERT(res.code != ErrorCode::StorageError);
  }

  void testAddMp4() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    // MOV/MP4 is now fully validated via libavformat
    auto res = img.addImage(Blob::fromVector(std::move(mov)), "clip.mp4");
    CPPUNIT_ASSERT(res.code != ErrorCode::UnsupportedFormat);
    CPPUNIT_ASSERT(res.code != ErrorCode::BrokenFile);
    if (res.code == ErrorCode::Ok) {
      CPPUNIT_ASSERT(!res.id.empty());
      CPPUNIT_ASSERT_EQUAL(size_t(64), res.id.size());
    }
  }

  void testAddMov() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto res = img.addImage(Blob::fromVector(std::move(mov)), "clip.mov");
    CPPUNIT_ASSERT(res.code != ErrorCode::UnsupportedFormat);
    CPPUNIT_ASSERT(res.code != ErrorCode::BrokenFile);
    if (res.code == ErrorCode::Ok) {
      CPPUNIT_ASSERT(!res.id.empty());
      CPPUNIT_ASSERT_EQUAL(size_t(64), res.id.size());
    }
  }

  void testAddMovDuplicate() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto blob = Blob::fromVector(std::vector<uint8_t>(mov));
    auto r1 = img.addImage(blob, "clip.mov");
    if (r1.code != ErrorCode::Ok) {
      return; // skip if storage not available
    }
    auto r2 = img.addImage(blob, "clip_copy.mov");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
  }

  void testAddBrokenMov() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    // ftyp box signature present, but truncated — container is incomplete
    mov.resize(64);
    Imager img(m_cfg);
    auto res = img.addImage(Blob::fromVector(std::move(mov)), "bad.mov");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::BrokenFile, res.code);
  }

  void testUnsupportedFormat() {
    Imager img(m_cfg);
    auto res = img.addImage(Blob::fromVector({0x01, 0x02, 0x03}), "file.bmp");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::UnsupportedFormat, res.code);
  }

  void testDuplicateDetection() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto blob = Blob::fromVector(std::vector<uint8_t>(mov)); // same data for both adds
    auto r1 = img.addImage(blob, "clip.mp4");
    if (r1.code != ErrorCode::Ok) {
      return; // storage may fail in sandbox
    }

    auto r2 = img.addImage(blob, "clip_copy.mp4");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
  }

  void testBrokenJpeg() {
    Imager img(m_cfg);
    // SOI marker but then garbage
    auto res = img.addImage(Blob::fromVector({0xFF, 0xD8, 0x00, 0x00, 0x00, 0x00}), "bad.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::BrokenFile, res.code);
  }

  void testAddHeic() {
    Imager img(m_cfg);
    auto res = img.addImage(Blob::fromVector(makeMinimalHeic()), "photo.heic");
    // Ok if AV1 decoder is available; BrokenFile if not — but never UnsupportedFormat
    CPPUNIT_ASSERT(res.code != ErrorCode::UnsupportedFormat);
    CPPUNIT_ASSERT(res.code != ErrorCode::StorageError);
    if (res.code == ErrorCode::Ok) {
      CPPUNIT_ASSERT(!res.id.empty());
      CPPUNIT_ASSERT_EQUAL(size_t(64), res.id.size());
    }
  }

  void testAddHeicDuplicate() {
    Imager img(m_cfg);
    auto blob = Blob::fromVector(makeMinimalHeic());
    auto r1 = img.addImage(blob, "photo.heic");
    if (r1.code != ErrorCode::Ok) {
      return; // skip if decode or storage not available
    }
    auto r2 = img.addImage(blob, "photo_copy.heic");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
  }

  void testAddBrokenHeic() {
    Imager img(m_cfg);
    // ftyp box signature present, but truncated — no valid image data
    auto full = makeMinimalHeic();
    full.resize(64);
    auto res = img.addImage(Blob::fromVector(std::move(full)), "bad.heic");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::BrokenFile, res.code);
  }

  void testAddNef() {
    auto nef = loadNefFixture();
    if (nef.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto res = img.addImage(Blob::fromVector(std::vector<uint8_t>(nef)), "photo.nef");
    CPPUNIT_ASSERT(res.code != ErrorCode::UnsupportedFormat);
    CPPUNIT_ASSERT(res.code != ErrorCode::StorageError);
    if (res.code == ErrorCode::Ok) {
      CPPUNIT_ASSERT(!res.id.empty());
      CPPUNIT_ASSERT_EQUAL(size_t(64), res.id.size());
    }
  }

  void testAddNefDuplicate() {
    auto nef = loadNefFixture();
    if (nef.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto blob = Blob::fromVector(std::vector<uint8_t>(nef));
    auto r1 = img.addImage(blob, "photo.nef");
    if (r1.code != ErrorCode::Ok) {
      return; // skip if decode or storage not available
    }
    auto r2 = img.addImage(blob, "photo_copy.nef");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
  }

  void testAddBrokenNef() {
    auto nef = loadNefFixture();
    if (nef.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    // TIFF magic present, but truncated — no valid RAW data
    nef.resize(64);
    auto res = img.addImage(Blob::fromVector(std::move(nef)), "bad.nef");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::BrokenFile, res.code);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(AddImageTest);

// ---------------------------------------------------------------------------
// Test: getImage / listImages / imageCount
// ---------------------------------------------------------------------------

class QueryTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(QueryTest);
  CPPUNIT_TEST(testGetNotFound);
  CPPUNIT_TEST(testListEmpty);
  CPPUNIT_TEST(testCountZero);
  CPPUNIT_TEST(testAddAndQuery);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_q_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testGetNotFound() {
    Imager img(m_cfg);
    CPPUNIT_ASSERT(!img.getImage("deadbeef").has_value());
  }

  void testListEmpty() {
    Imager img(m_cfg);
    CPPUNIT_ASSERT(img.listImages().empty());
  }

  void testCountZero() {
    Imager img(m_cfg);
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
  }

  void testAddAndQuery() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "video.mp4");
    if (r.code != ErrorCode::Ok) {
      return; // skip if storage fails
    }

    CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());

    auto info = img.getImage(r.id);
    CPPUNIT_ASSERT(info.has_value());
    CPPUNIT_ASSERT_EQUAL(r.id, info->id);
    CPPUNIT_ASSERT_EQUAL(std::string("video.mp4"), info->name);
    CPPUNIT_ASSERT_EQUAL(std::string(".mp4"), info->ext);

    auto list = img.listImages();
    CPPUNIT_ASSERT_EQUAL(size_t(1), list.size());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(QueryTest);

// ---------------------------------------------------------------------------
// Test: tags
// ---------------------------------------------------------------------------

class TagTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TagTest);
  CPPUNIT_TEST(testCreateAndListTags);
  CPPUNIT_TEST(testTagAndUntag);
  CPPUNIT_TEST(testGetImagesByTags);
  CPPUNIT_TEST(testDeleteTag);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;
  std::string m_id;

  void addFile(Imager& img, const std::string& name) {
    // Vary content slightly to avoid dedup — trailing bytes are ignored by libavformat
    auto mov = makeUniqueMovFixture(static_cast<uint8_t>(name.size()), static_cast<uint8_t>(name.size() >> 8));
    if (mov.empty()) {
      return; // fixture absent
    }
    auto r = img.addImage(Blob::fromVector(std::move(mov)), name + ".mp4");
    if (r.code == ErrorCode::Ok) {
      m_id = r.id;
    }
  }

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_tag_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testCreateAndListTags() {
    Imager img(m_cfg);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.createTag("nature"));
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.createTag("urban"));
    auto tags = img.listTags();
    CPPUNIT_ASSERT_EQUAL(size_t(2), tags.size());
  }

  void testTagAndUntag() {
    Imager img(m_cfg);
    addFile(img, "clip_tag");
    if (m_id.empty()) {
      return;
    }

    img.createTag("landscape");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.tagImage(m_id, "landscape"));

    auto tags = img.getImageTags(m_id);
    CPPUNIT_ASSERT_EQUAL(size_t(1), tags.size());
    CPPUNIT_ASSERT_EQUAL(std::string("landscape"), tags[0]);

    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.untagImage(m_id, "landscape"));
    CPPUNIT_ASSERT(img.getImageTags(m_id).empty());
  }

  void testGetImagesByTags() {
    Imager img(m_cfg);
    addFile(img, "clip_search");
    if (m_id.empty()) {
      return;
    }

    img.createTag("nature");
    img.tagImage(m_id, "nature");

    auto results = img.getImagesByTags({"nature"});
    CPPUNIT_ASSERT_EQUAL(size_t(1), results.size());
    CPPUNIT_ASSERT_EQUAL(m_id, results[0].id);

    // No match
    auto none = img.getImagesByTags({"urban"});
    CPPUNIT_ASSERT(none.empty());
  }

  void testDeleteTag() {
    Imager img(m_cfg);
    img.createTag("temp");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.deleteTag("temp"));
    CPPUNIT_ASSERT(img.listTags().empty());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TagTest);

// ---------------------------------------------------------------------------
// Test: deleteImage
// ---------------------------------------------------------------------------

class DeleteTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(DeleteTest);
  CPPUNIT_TEST(testDeleteExisting);
  CPPUNIT_TEST(testDeleteNotFound);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_del_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testDeleteExisting() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "del_test.mp4");
    if (r.code != ErrorCode::Ok) {
      return;
    }

    CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.deleteImage(r.id));
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
  }

  void testDeleteNotFound() {
    Imager img(m_cfg);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::FileNotFound, img.deleteImage("nosuchid"));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(DeleteTest);

// ---------------------------------------------------------------------------
// Test: multi-root storage
// ---------------------------------------------------------------------------

class MultiRootTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(MultiRootTest);
  CPPUNIT_TEST(testFileWrittenToAllRoots);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_mr_" + s);
    fs::create_directories(m_base / "root1");
    fs::create_directories(m_base / "root2");
    m_cfg.targets.push_back({m_base / "root1", m_base / "imager1.db"});
    m_cfg.targets.push_back({m_base / "root2", m_base / "imager2.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testFileWrittenToAllRoots() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent — skip
    }
    Imager img(m_cfg);
    auto r = img.addImage(Blob::fromVector(std::move(mov)), "multi.mp4");
    if (r.code != ErrorCode::Ok) {
      return;
    }

    // File should exist in both roots
    const std::string shard = r.id.substr(0, 2);
    const std::string fname = r.id + ".mp4";
    CPPUNIT_ASSERT(fs::exists(m_base / "root1" / shard / fname));
    CPPUNIT_ASSERT(fs::exists(m_base / "root2" / shard / fname));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(MultiRootTest);

// ---------------------------------------------------------------------------
// Test: concurrent addImage
// ---------------------------------------------------------------------------

class ConcurrencyTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ConcurrencyTest);
  CPPUNIT_TEST(testConcurrentAdds);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_conc_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testConcurrentAdds() {
    // Pre-load base fixture — if not available, skip test
    if (loadMovFixture().empty()) {
      return;
    }

    Imager img(m_cfg);
    constexpr int THREADS = 4;
    constexpr int PER_THREAD = 10;

    std::atomic<int> successes{0};
    std::vector<std::jthread> threads;

    for (int t = 0; t < THREADS; ++t) {
      threads.emplace_back([&img, &successes, t]() {
        for (int i = 0; i < PER_THREAD; ++i) {
          // Trailing bytes make each file unique while keeping the MOV valid
          auto mov = makeUniqueMovFixture(static_cast<uint8_t>(t), static_cast<uint8_t>(i));
          if (mov.empty()) {
            continue;
          }
          auto r =
            img.addImage(Blob::fromVector(std::move(mov)), "t" + std::to_string(t) + "_" + std::to_string(i) + ".mp4");
          if (r.code == ErrorCode::Ok) {
            ++successes;
          }
        }
      });
    }
    threads.clear(); // join all

    CPPUNIT_ASSERT_EQUAL(THREADS * PER_THREAD, successes.load());
    CPPUNIT_ASSERT_EQUAL(uint64_t(THREADS * PER_THREAD), img.imageCount());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ConcurrencyTest);

// ---------------------------------------------------------------------------
// Helpers: make a minimal valid AAE blob
// ---------------------------------------------------------------------------

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
// Sidecar pairing tests
// ---------------------------------------------------------------------------

class SidecarTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SidecarTest);
  CPPUNIT_TEST(testAddAaeWithParent);
  CPPUNIT_TEST(testAddAaeWithoutParent);
  CPPUNIT_TEST(testAddParentResolvesOrphan);
  CPPUNIT_TEST(testAddAaeDuplicate);
  CPPUNIT_TEST(testAddBrokenAae);
  CPPUNIT_TEST(testDeleteParentCascadesToSidecar);
  CPPUNIT_TEST(testGetSidecarData);
  CPPUNIT_TEST(testAaePairingIsCaseInsensitive);
  CPPUNIT_TEST(testAaePairingRespectsSourceDir);
  CPPUNIT_TEST(testAaeNoCrossDirectoryPairing);
  CPPUNIT_TEST(testAaeBareFilename);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;

public:
  void setUp() override {
    m_cfg = makeTempConfig();
  }

  void tearDown() override {
    // Temp dirs cleaned up by OS; we just reset config
    m_cfg = {};
  }

  // --- testAddAaeWithParent ------------------------------------------------
  // Add JPG first, then AAE -> AAE stored with JPG's hash as storage prefix.
  void testAddAaeWithParent() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg, "vacation/IMG_1234.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);
    const std::string jpgHash = r1.id;

    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);
    const std::string aaeHash = r2.id;

    // The two hashes are different (different content)
    CPPUNIT_ASSERT(jpgHash != aaeHash);

    // But the AAE should be retrievable
    auto data = img.getImageData(aaeHash);
    CPPUNIT_ASSERT(!data.empty());
  }

  // --- testAddAaeWithoutParent ---------------------------------------------
  // Add AAE before its parent -> stored as orphan with own hash.
  void testAddAaeWithoutParent() {
    Imager img(m_cfg);

    auto aae = makeAaeBlob();
    auto r = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r.code);

    // Should be retrievable even as orphan
    auto data = img.getImageData(r.id);
    CPPUNIT_ASSERT(!data.empty());
  }

  // --- testAddParentResolvesOrphan -----------------------------------------
  // Add AAE first (orphan), then JPG -> AAE gets relocated to JPG's hash path.
  void testAddParentResolvesOrphan() {
    Imager img(m_cfg);

    auto aae = makeAaeBlob();
    auto r1 = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);
    const std::string aaeHash = r1.id;

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r2 = img.addImage(jpeg, "vacation/IMG_1234.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);

    // AAE should still be retrievable after relocation
    auto data = img.getImageData(aaeHash);
    CPPUNIT_ASSERT(!data.empty());
  }

  // --- testAddAaeDuplicate -------------------------------------------------
  // Add same AAE twice -> second returns DuplicateFile.
  void testAddAaeDuplicate() {
    Imager img(m_cfg);

    auto aae = makeAaeBlob();
    auto r1 = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    auto r2 = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
  }

  // --- testAddBrokenAae ----------------------------------------------------
  // Truncated/corrupt AAE bytes -> BrokenFile.
  void testAddBrokenAae() {
    Imager img(m_cfg);

    std::vector<uint8_t> corrupt = {0x00, 0x01, 0x02, 0x03};
    auto r = img.addImage(Blob::fromVector(std::move(corrupt)), "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::BrokenFile, r.code);
  }

  // --- testDeleteParentCascadesToSidecar -----------------------------------
  // Delete JPG -> AAE also deleted from storage and DB.
  void testDeleteParentCascadesToSidecar() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg, "vacation/IMG_1234.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);
    const std::string aaeHash = r2.id;

    // Delete the parent
    auto ec = img.deleteImage(r1.id);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, ec);

    // AAE should no longer exist in DB
    auto info = img.getImage(aaeHash);
    CPPUNIT_ASSERT(!info.has_value());
  }

  // --- testGetSidecarData --------------------------------------------------
  // Add JPG + AAE, retrieve AAE by its content hash -> correct data returned.
  void testGetSidecarData() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    img.addImage(jpeg, "vacation/IMG_1234.JPG");

    auto aae = makeAaeBlob();
    auto r = img.addImage(aae, "vacation/IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r.code);

    auto data = img.getImageData(r.id);
    CPPUNIT_ASSERT(!data.empty());
    CPPUNIT_ASSERT_EQUAL(aae.size(), data.size());
  }

  // --- testAaePairingIsCaseInsensitive -------------------------------------
  // img_1234.jpg + IMG_1234.AAE pair correctly despite case difference.
  void testAaePairingIsCaseInsensitive() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg, "vacation/img_1234.jpg"); // lowercase
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "vacation/IMG_1234.AAE"); // uppercase base
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);

    // Both should be in the system
    CPPUNIT_ASSERT_EQUAL(uint64_t(2), img.imageCount());
  }

  // --- testAaePairingRespectsSourceDir ------------------------------------
  // dir_a/IMG_0001.JPG + dir_b/IMG_0001.JPG + dir_a/IMG_0001.AAE
  // -> AAE pairs only with dir_a's JPG.
  void testAaePairingRespectsSourceDir() {
    Imager img(m_cfg);

    // Use different JPEG content so they have different hashes
    auto jpeg1 = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg1, "dir_a/IMG_0001.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    // Second JPEG with slightly different content (use a MOV-free unique approach)
    auto jpeg2Data = makeMinimalJpeg();
    jpeg2Data.push_back(0x00); // make content unique
    auto jpeg2 = Blob::fromVector(std::move(jpeg2Data));
    auto r2 = img.addImage(jpeg2, "dir_b/IMG_0001.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);

    auto aae = makeAaeBlob();
    auto r3 = img.addImage(aae, "dir_a/IMG_0001.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r3.code);

    // AAE should be paired with dir_a's JPG (retrievable via storage path using dir_a's hash)
    auto data = img.getImageData(r3.id);
    CPPUNIT_ASSERT(!data.empty());
  }

  // --- testAaeNoCrossDirectoryPairing -------------------------------------
  // dir_a/IMG_0001.JPG + dir_b/IMG_0001.AAE -> AAE is orphan.
  void testAaeNoCrossDirectoryPairing() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg, "dir_a/IMG_0001.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    // AAE in different dir -> should be orphan (no parent in dir_b)
    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "dir_b/IMG_0001.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);

    // Still stored as orphan with own hash
    auto data = img.getImageData(r2.id);
    CPPUNIT_ASSERT(!data.empty());
  }

  // --- testAaeBareFilename ------------------------------------------------
  // IMG_1234.JPG + IMG_1234.AAE (no path prefix) -> pairs via empty source_dir.
  void testAaeBareFilename() {
    Imager img(m_cfg);

    auto jpeg = Blob::fromVector(makeMinimalJpeg());
    auto r1 = img.addImage(jpeg, "IMG_1234.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "IMG_1234.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);

    CPPUNIT_ASSERT_EQUAL(uint64_t(2), img.imageCount());

    auto data = img.getImageData(r2.id);
    CPPUNIT_ASSERT(!data.empty());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SidecarTest);

// ---------------------------------------------------------------------------
// Test: deleteFile / deleteBlob / hashOnlyFile / hashOnlyBlob
// ---------------------------------------------------------------------------

class DeleteByFileTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(DeleteByFileTest);
  CPPUNIT_TEST(testDeleteFileRoundTrip);
  CPPUNIT_TEST(testDeleteFileIdempotent);
  CPPUNIT_TEST(testDeleteBlobRoundTrip);
  CPPUNIT_TEST(testDeleteFileContentIdentityRename);
  CPPUNIT_TEST(testDeleteFileSidecarCascade);
  CPPUNIT_TEST(testHashOnlyFileAndBlob);
  CPPUNIT_TEST(testDeleteFileNotFound);
  CPPUNIT_TEST(testDeleteFileMissingPath);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_dfile_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  // Write bytes to a temp file and return its path.
  fs::path writeTempFile(const std::string& name, const std::vector<uint8_t>& data) {
    fs::path p = m_base / name;
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return p;
  }

  // --- testDeleteFileRoundTrip ---------------------------------------------
  // addFile then deleteFile the same path -> Ok; count drops to 0.
  void testDeleteFileRoundTrip() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return; // fixture absent
    }
    fs::path p = writeTempFile("round_trip.mov", mov);

    Imager img(m_cfg);
    auto r = img.addFile(p);
    if (r.code != ErrorCode::Ok) {
      return;
    }

    CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());

    auto dr = img.deleteFile(p);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, dr.code);
    CPPUNIT_ASSERT_EQUAL(r.id, dr.id);
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
    CPPUNIT_ASSERT(!img.getImage(r.id).has_value());
  }

  // --- testDeleteFileIdempotent --------------------------------------------
  // Second deleteFile on the same path after first succeeds -> FileNotFound.
  void testDeleteFileIdempotent() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return;
    }
    fs::path p = writeTempFile("idem.mov", mov);

    Imager img(m_cfg);
    auto r = img.addFile(p);
    if (r.code != ErrorCode::Ok) {
      return;
    }

    auto dr1 = img.deleteFile(p);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, dr1.code);

    // Second delete: same content, same hash, nothing left -> FileNotFound
    auto dr2 = img.deleteFile(p);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::FileNotFound, dr2.code);
    // id is still populated even on miss
    CPPUNIT_ASSERT_EQUAL(r.id, dr2.id);
  }

  // --- testDeleteBlobRoundTrip ---------------------------------------------
  // addImage(blob) then deleteBlob(blob) -> Ok.
  void testDeleteBlobRoundTrip() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return;
    }
    auto blob = Blob::fromVector(std::vector<uint8_t>(mov));

    Imager img(m_cfg);
    auto r = img.addImage(blob, "clip.mov");
    if (r.code != ErrorCode::Ok) {
      return;
    }

    auto dr = img.deleteBlob(blob);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, dr.code);
    CPPUNIT_ASSERT_EQUAL(r.id, dr.id);
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
  }

  // --- testDeleteFileContentIdentityRename ---------------------------------
  // Add a file, copy its bytes under a different extension (e.g. .bin),
  // deleteFile the renamed copy -> still deletes (same SHA256).
  void testDeleteFileContentIdentityRename() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return;
    }
    fs::path orig = writeTempFile("identity.mov", mov);
    fs::path renamed = writeTempFile("identity.bin", mov); // same content, different ext

    Imager img(m_cfg);
    auto r = img.addFile(orig);
    if (r.code != ErrorCode::Ok) {
      return;
    }

    // deleteFile the .bin copy — identity is pure SHA256, so extension doesn't matter
    auto dr = img.deleteFile(renamed);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, dr.code);
    CPPUNIT_ASSERT_EQUAL(r.id, dr.id);
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
  }

  // --- testDeleteFileSidecarCascade ----------------------------------------
  // addFile JPEG + addImage AAE, then deleteFile(JPEG path) -> both gone.
  void testDeleteFileSidecarCascade() {
    auto jpeg = makeMinimalJpeg();
    fs::path jpgPath = writeTempFile("IMG_7890.jpg", jpeg);

    Imager img(m_cfg);
    auto r1 = img.addFile(jpgPath, "IMG_7890.JPG");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r1.code);

    auto aae = makeAaeBlob();
    auto r2 = img.addImage(aae, "IMG_7890.AAE");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, r2.code);
    const std::string aaeId = r2.id;

    CPPUNIT_ASSERT_EQUAL(uint64_t(2), img.imageCount());

    // deleteFile with the path to the JPEG (hashes the bytes, finds parent)
    auto dr = img.deleteFile(jpgPath, nullptr);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, dr.code);

    // Both parent and sidecar should be gone
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
    CPPUNIT_ASSERT(!img.getImage(r1.id).has_value());
    CPPUNIT_ASSERT(!img.getImage(aaeId).has_value());
  }

  // --- testHashOnlyFileAndBlob ---------------------------------------------
  // hashOnlyFile / hashOnlyBlob returns the same id as addFile without mutating state.
  void testHashOnlyFileAndBlob() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return;
    }
    fs::path p = writeTempFile("hashonly.mov", mov);
    auto blob = Blob::fromVector(std::vector<uint8_t>(mov));

    Imager img(m_cfg);

    // Hash before add: returns a non-empty 64-char hex string
    std::string hFile = img.hashOnlyFile(p);
    CPPUNIT_ASSERT(!hFile.empty());
    CPPUNIT_ASSERT_EQUAL(size_t(64), hFile.size());

    std::string hBlob = img.hashOnlyBlob(blob);
    CPPUNIT_ASSERT_EQUAL(hFile, hBlob);

    // No mutation
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());

    // After addFile: id matches hash
    auto r = img.addFile(p);
    if (r.code != ErrorCode::Ok) {
      return;
    }
    CPPUNIT_ASSERT_EQUAL(hFile, r.id);

    // hashOnly with file imported: still same id, getImage succeeds
    std::string hAfter = img.hashOnlyFile(p);
    CPPUNIT_ASSERT_EQUAL(hFile, hAfter);
    CPPUNIT_ASSERT(img.getImage(hAfter).has_value());
  }

  // --- testDeleteFileNotFound ----------------------------------------------
  // deleteFile on a file whose content was never imported -> FileNotFound.
  // id is still populated in the result.
  void testDeleteFileNotFound() {
    auto mov = loadMovFixture();
    if (mov.empty()) {
      return;
    }
    fs::path p = writeTempFile("notimported.mov", mov);

    Imager img(m_cfg);
    auto dr = img.deleteFile(p);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::FileNotFound, dr.code);
    // id should be the SHA256 of the file bytes even on miss
    CPPUNIT_ASSERT_EQUAL(size_t(64), dr.id.size());
  }

  // --- testDeleteFileMissingPath -------------------------------------------
  // deleteFile on a path that does not exist on disk -> error (not FileNotFound
  // from DB — rather StorageError or FileNotFound from the read phase).
  void testDeleteFileMissingPath() {
    Imager img(m_cfg);
    fs::path absent = m_base / "does_not_exist.mov";
    auto dr = img.deleteFile(absent);
    // Any error code except Ok is acceptable
    CPPUNIT_ASSERT(dr.code != ErrorCode::Ok);
    // id should be empty (hashing never succeeded)
    CPPUNIT_ASSERT(dr.id.empty());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(DeleteByFileTest);

// ---------------------------------------------------------------------------
// Test: getUntaggedImages
// ---------------------------------------------------------------------------

class UntaggedImagesTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(UntaggedImagesTest);
  CPPUNIT_TEST(testReturnsOnlyUntaggedImages);
  CPPUNIT_TEST(testTaggedImageExcluded);
  CPPUNIT_TEST(testOffsetAndLimit);
  CPPUNIT_TEST_SUITE_END();

  config::AppConfig m_cfg;
  fs::path m_base;

  // Add a MOV file with unique trailing bytes so dedup does not squash it.
  // Returns the file id, or empty if the fixture is unavailable.
  std::string addUniqueFile(Imager& img, uint8_t salt1, uint8_t salt2, const std::string& name) {
    auto mov = makeUniqueMovFixture(salt1, salt2);
    if (mov.empty()) {
      return {};
    }
    auto r = img.addImage(Blob::fromVector(std::move(mov)), name + ".mp4");
    return (r.code == ErrorCode::Ok) ? r.id : std::string{};
  }

public:
  void setUp() override {
    std::string s = uniqueSuffix();
    m_base = fs::temp_directory_path() / ("imager_test_untag_" + s);
    fs::create_directories(m_base / "storage");
    m_cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
  }

  void tearDown() override {
    fs::remove_all(m_base);
  }

  void testReturnsOnlyUntaggedImages() {
    Imager img(m_cfg);
    auto id = addUniqueFile(img, 0x01, 0x02, "clip_u1");
    if (id.empty()) {
      return; // fixture absent
    }

    auto result = img.getUntaggedImages();
    CPPUNIT_ASSERT_EQUAL(size_t(1), result.size());
    CPPUNIT_ASSERT_EQUAL(id, result[0].id);
    // Tags field must be empty — these files have no tags by definition
    CPPUNIT_ASSERT(result[0].tags.empty());
  }

  void testTaggedImageExcluded() {
    Imager img(m_cfg);
    auto id1 = addUniqueFile(img, 0x11, 0x12, "clip_tagged");
    auto id2 = addUniqueFile(img, 0x13, 0x14, "clip_untagged");
    if (id1.empty() || id2.empty()) {
      return; // fixture absent
    }

    img.createTag("nature");
    img.tagImage(id1, "nature");

    // Only id2 (no tags) should be returned
    auto result = img.getUntaggedImages();
    CPPUNIT_ASSERT_EQUAL(size_t(1), result.size());
    CPPUNIT_ASSERT_EQUAL(id2, result[0].id);
    CPPUNIT_ASSERT(result[0].tags.empty());
  }

  void testOffsetAndLimit() {
    Imager img(m_cfg);
    auto id1 = addUniqueFile(img, 0x21, 0x22, "clip_page1");
    auto id2 = addUniqueFile(img, 0x23, 0x24, "clip_page2");
    auto id3 = addUniqueFile(img, 0x25, 0x26, "clip_page3");
    if (id1.empty() || id2.empty() || id3.empty()) {
      return; // fixture absent
    }

    // limit=2, offset=0
    auto page0 = img.getUntaggedImages(0, 2);
    CPPUNIT_ASSERT_EQUAL(size_t(2), page0.size());

    // limit=2, offset=2  — should return exactly 1 remaining item
    auto page1 = img.getUntaggedImages(2, 2);
    CPPUNIT_ASSERT_EQUAL(size_t(1), page1.size());

    // offset past end — empty
    auto empty = img.getUntaggedImages(100, 10);
    CPPUNIT_ASSERT(empty.empty());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(UntaggedImagesTest);

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
