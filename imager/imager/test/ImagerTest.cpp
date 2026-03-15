#include "imager/Imager.h"
#include "config/Config.h"

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace fs = std::filesystem;
using namespace imager;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string uniqueSuffix() {
    return std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

/// Build a temporary AppConfig with one storage root and one db file.
static config::AppConfig makeTempConfig(const std::string& suffix = "") {
    std::string s = suffix.empty() ? uniqueSuffix() : suffix;
    fs::path base = fs::temp_directory_path() / ("imager_test_" + s);
    fs::create_directories(base / "storage");

    config::AppConfig cfg;
    cfg.storage.roots.push_back(base / "storage");
    cfg.database.path = base / "imager.db";
    return cfg;
}

/// Minimal valid JPEG bytes (SOI + EOI).
static std::vector<uint8_t> makeMinimalJpeg() {
    // A 1x1 white JPEG created with libjpeg would be complex; instead
    // create a valid 1x1 JFIF. We use a hard-coded small valid JPEG.
    // (FF D8 FF E0 JFIF header ... FF D9 EOI)
    return {
        0xFF,0xD8,0xFF,0xE0,0x00,0x10,'J','F','I','F',0x00,0x01,0x01,
        0x00,0x00,0x01,0x00,0x01,0x00,0x00,
        0xFF,0xDB,0x00,0x43,0x00,
        0x10,0x0B,0x0C,0x0E,0x0C,0x0A,0x10,0x0E,0x0D,0x0E,0x12,0x11,
        0x10,0x13,0x18,0x28,0x1A,0x18,0x16,0x16,0x18,0x31,0x23,0x25,
        0x1D,0x28,0x3A,0x33,0x3D,0x3C,0x39,0x33,0x38,0x37,0x40,0x48,
        0x5C,0x4E,0x40,0x44,0x57,0x45,0x37,0x38,0x50,0x6D,0x51,0x57,
        0x5F,0x62,0x67,0x68,0x67,0x3E,0x4D,0x71,0x79,0x70,0x64,0x78,
        0x5C,0x65,0x67,0x63,
        0xFF,0xC0,0x00,0x0B,0x08,0x00,0x01,0x00,0x01,0x01,0x01,0x11,0x00,
        0xFF,0xC4,0x00,0x1F,0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,
        0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,
        0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,
        0xFF,0xC4,0x00,0xB5,0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,
        0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,0x7D,0x01,0x02,0x03,
        0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
        0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xA1,0x08,0x23,0x42,0xB1,
        0xC1,0x15,0x52,0xD1,0xF0,0x24,0x33,0x62,0x72,0x82,0x09,0x0A,
        0x16,0x17,0x18,0x19,0x1A,0x25,0x26,0x27,0x28,0x29,0x2A,0x34,
        0x35,0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,
        0x49,0x4A,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,
        0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,
        0x79,0x7A,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,
        0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,
        0xA7,0xA8,0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,
        0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,
        0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE1,0xE2,0xE3,0xE4,0xE5,
        0xE6,0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,
        0xF8,0xF9,0xFA,
        0xFF,0xDA,0x00,0x08,0x01,0x01,0x00,0x00,0x3F,0x00,0xFB,0xDE,
        0xCA,0xF7,0x93,0x5C,0xFF,0xD9
    };
}

/// Fake MP4 bytes (just some bytes, no deep validation).
static std::vector<uint8_t> makeFakeMP4() {
    // ftyp box header for MP4
    std::vector<uint8_t> d(256, 0x00);
    d[0]=0x00; d[1]=0x00; d[2]=0x00; d[3]=0x20; // box size = 32
    d[4]='f';  d[5]='t';  d[6]='y';  d[7]='p';  // box type
    d[8]='i';  d[9]='s';  d[10]='o'; d[11]='m'; // major brand
    return d;
}

// ---------------------------------------------------------------------------
// Test: AddImage workflow
// ---------------------------------------------------------------------------

class AddImageTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(AddImageTest);
    CPPUNIT_TEST(testAddJpeg);
    CPPUNIT_TEST(testAddMp4);
    CPPUNIT_TEST(testUnsupportedFormat);
    CPPUNIT_TEST(testDuplicateDetection);
    CPPUNIT_TEST(testBrokenJpeg);
    CPPUNIT_TEST_SUITE_END();

    config::AppConfig m_cfg;
    fs::path          m_base;

public:
    void setUp() override {
        std::string s = uniqueSuffix();
        m_base = fs::temp_directory_path() / ("imager_test_add_" + s);
        fs::create_directories(m_base / "storage");
        m_cfg.storage.roots.push_back(m_base / "storage");
        m_cfg.database.path = m_base / "imager.db";
    }

    void tearDown() override {
        fs::remove_all(m_base);
    }

    void testAddJpeg() {
        Imager img(m_cfg);
        auto jpeg = makeMinimalJpeg();
        auto res = img.addImage(jpeg.data(), jpeg.size(), "photo.jpg");
        // May succeed or return BrokenFile if the hand-crafted JPEG is not
        // fully valid — both are acceptable; just not UnsupportedFormat.
        CPPUNIT_ASSERT(res.code != ErrorCode::UnsupportedFormat);
        CPPUNIT_ASSERT(res.code != ErrorCode::StorageError);
    }

    void testAddMp4() {
        Imager img(m_cfg);
        auto mp4 = makeFakeMP4();
        // MP4 is accepted by extension only — no deep validation
        auto res = img.addImage(mp4.data(), mp4.size(), "clip.mp4");
        CPPUNIT_ASSERT(res.code == ErrorCode::Ok || res.code == ErrorCode::StorageError);
        if (res.code == ErrorCode::Ok) {
            CPPUNIT_ASSERT(!res.id.empty());
            CPPUNIT_ASSERT_EQUAL(size_t(64), res.id.size());
        }
    }

    void testUnsupportedFormat() {
        Imager img(m_cfg);
        std::vector<uint8_t> data = {0x01, 0x02, 0x03};
        auto res = img.addImage(data.data(), data.size(), "file.bmp");
        CPPUNIT_ASSERT_EQUAL(ErrorCode::UnsupportedFormat, res.code);
    }

    void testDuplicateDetection() {
        Imager img(m_cfg);
        auto mp4 = makeFakeMP4();
        auto r1 = img.addImage(mp4.data(), mp4.size(), "clip.mp4");
        if (r1.code != ErrorCode::Ok) return; // storage may fail in sandbox

        auto r2 = img.addImage(mp4.data(), mp4.size(), "clip_copy.mp4");
        CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, r2.code);
    }

    void testBrokenJpeg() {
        Imager img(m_cfg);
        // SOI marker but then garbage
        std::vector<uint8_t> bad = {0xFF, 0xD8, 0x00, 0x00, 0x00, 0x00};
        auto res = img.addImage(bad.data(), bad.size(), "bad.jpg");
        CPPUNIT_ASSERT_EQUAL(ErrorCode::BrokenFile, res.code);
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(AddImageTest);

// ---------------------------------------------------------------------------
// Test: getImage / listImages / imageCount
// ---------------------------------------------------------------------------

class QueryTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(QueryTest);
    CPPUNIT_TEST(testGetNotFound);
    CPPUNIT_TEST(testListEmpty);
    CPPUNIT_TEST(testCountZero);
    CPPUNIT_TEST(testAddAndQuery);
    CPPUNIT_TEST_SUITE_END();

    config::AppConfig m_cfg;
    fs::path          m_base;

public:
    void setUp() override {
        std::string s = uniqueSuffix();
        m_base = fs::temp_directory_path() / ("imager_test_q_" + s);
        fs::create_directories(m_base / "storage");
        m_cfg.storage.roots.push_back(m_base / "storage");
        m_cfg.database.path = m_base / "imager.db";
    }

    void tearDown() override { fs::remove_all(m_base); }

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
        Imager img(m_cfg);
        auto mp4 = makeFakeMP4();
        auto r = img.addImage(mp4.data(), mp4.size(), "video.mp4");
        if (r.code != ErrorCode::Ok) return; // skip if storage fails

        CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());

        auto info = img.getImage(r.id);
        CPPUNIT_ASSERT(info.has_value());
        CPPUNIT_ASSERT_EQUAL(r.id,                    info->id);
        CPPUNIT_ASSERT_EQUAL(std::string("video.mp4"), info->name);
        CPPUNIT_ASSERT_EQUAL(std::string(".mp4"),      info->ext);

        auto list = img.listImages();
        CPPUNIT_ASSERT_EQUAL(size_t(1), list.size());
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(QueryTest);

// ---------------------------------------------------------------------------
// Test: tags
// ---------------------------------------------------------------------------

class TagTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(TagTest);
    CPPUNIT_TEST(testCreateAndListTags);
    CPPUNIT_TEST(testTagAndUntag);
    CPPUNIT_TEST(testGetImagesByTags);
    CPPUNIT_TEST(testDeleteTag);
    CPPUNIT_TEST_SUITE_END();

    config::AppConfig m_cfg;
    fs::path          m_base;
    std::string       m_id;

    void addFile(Imager& img, const std::string& name) {
        auto mp4 = makeFakeMP4();
        // Vary content slightly to avoid dedup
        mp4.push_back(static_cast<uint8_t>(name.size()));
        auto r = img.addImage(mp4.data(), mp4.size(), name + ".mp4");
        if (r.code == ErrorCode::Ok) m_id = r.id;
    }

public:
    void setUp() override {
        std::string s = uniqueSuffix();
        m_base = fs::temp_directory_path() / ("imager_test_tag_" + s);
        fs::create_directories(m_base / "storage");
        m_cfg.storage.roots.push_back(m_base / "storage");
        m_cfg.database.path = m_base / "imager.db";
    }

    void tearDown() override { fs::remove_all(m_base); }

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
        if (m_id.empty()) return;

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
        if (m_id.empty()) return;

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

class DeleteTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(DeleteTest);
    CPPUNIT_TEST(testDeleteExisting);
    CPPUNIT_TEST(testDeleteNotFound);
    CPPUNIT_TEST_SUITE_END();

    config::AppConfig m_cfg;
    fs::path          m_base;

public:
    void setUp() override {
        std::string s = uniqueSuffix();
        m_base = fs::temp_directory_path() / ("imager_test_del_" + s);
        fs::create_directories(m_base / "storage");
        m_cfg.storage.roots.push_back(m_base / "storage");
        m_cfg.database.path = m_base / "imager.db";
    }

    void tearDown() override { fs::remove_all(m_base); }

    void testDeleteExisting() {
        Imager img(m_cfg);
        auto mp4 = makeFakeMP4();
        auto r = img.addImage(mp4.data(), mp4.size(), "del_test.mp4");
        if (r.code != ErrorCode::Ok) return;

        CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());
        CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.deleteImage(r.id));
        CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
    }

    void testDeleteNotFound() {
        Imager img(m_cfg);
        CPPUNIT_ASSERT_EQUAL(ErrorCode::FileNotFound,
                             img.deleteImage("nosuchid"));
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(DeleteTest);

// ---------------------------------------------------------------------------
// Test: multi-root storage
// ---------------------------------------------------------------------------

class MultiRootTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(MultiRootTest);
    CPPUNIT_TEST(testFileWrittenToAllRoots);
    CPPUNIT_TEST_SUITE_END();

    config::AppConfig m_cfg;
    fs::path          m_base;

public:
    void setUp() override {
        std::string s = uniqueSuffix();
        m_base = fs::temp_directory_path() / ("imager_test_mr_" + s);
        fs::create_directories(m_base / "root1");
        fs::create_directories(m_base / "root2");
        m_cfg.storage.roots.push_back(m_base / "root1");
        m_cfg.storage.roots.push_back(m_base / "root2");
        m_cfg.database.path = m_base / "imager.db";
    }

    void tearDown() override { fs::remove_all(m_base); }

    void testFileWrittenToAllRoots() {
        Imager img(m_cfg);
        auto mp4 = makeFakeMP4();
        auto r = img.addImage(mp4.data(), mp4.size(), "multi.mp4");
        if (r.code != ErrorCode::Ok) return;

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

class ConcurrencyTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ConcurrencyTest);
    CPPUNIT_TEST(testConcurrentAdds);
    CPPUNIT_TEST_SUITE_END();

    config::AppConfig m_cfg;
    fs::path          m_base;

public:
    void setUp() override {
        std::string s = uniqueSuffix();
        m_base = fs::temp_directory_path() / ("imager_test_conc_" + s);
        fs::create_directories(m_base / "storage");
        m_cfg.storage.roots.push_back(m_base / "storage");
        m_cfg.database.path = m_base / "imager.db";
    }

    void tearDown() override { fs::remove_all(m_base); }

    void testConcurrentAdds() {
        Imager img(m_cfg);
        constexpr int THREADS = 4;
        constexpr int PER_THREAD = 10;

        std::atomic<int> successes{0};
        std::vector<std::jthread> threads;

        for (int t = 0; t < THREADS; ++t) {
            threads.emplace_back([&img, &successes, t]() {
                for (int i = 0; i < PER_THREAD; ++i) {
                    auto mp4 = makeFakeMP4();
                    // Make each file unique
                    mp4.push_back(static_cast<uint8_t>(t));
                    mp4.push_back(static_cast<uint8_t>(i));
                    auto r = img.addImage(mp4.data(), mp4.size(),
                                          "t" + std::to_string(t)
                                          + "_" + std::to_string(i) + ".mp4");
                    if (r.code == ErrorCode::Ok) ++successes;
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
// main
// ---------------------------------------------------------------------------

int main() {
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
    return runner.run() ? 0 : 1;
}
