// DuplicateIdTest.cpp — fix step fx2 / F2
//
// Imager::addFile on a duplicate returns ErrorCode::DuplicateFile AND the existing 64-hex id
// (previously the id was empty and only appeared in the message tail), on the in-memory path
// (<= 8 MB), the streamed path (> 8 MB), after a dry-run, and for AAE sidecars.

#include <config/Config.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <imager/Imager.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace imager;

namespace {

using Bytes = std::vector<uint8_t>;
constexpr uint64_t MB = 1024ULL * 1024ULL;

Bytes slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const fs::path& p, const Bytes& b) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!b.empty()) {
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  }
}

bool isHex64(const std::string& s) {
  if (s.size() != 64) {
    return false;
  }
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c)) && !(c >= 'a' && c <= 'f')) {
      return false;
    }
  }
  return true;
}

std::string aaeXml(int n) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n<dict>\n    <key>adjustmentFormatVersion</key>\n    <integer>" +
         std::to_string(n) + "</integer>\n</dict>\n</plist>\n";
}

} // namespace

class DuplicateIdTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(DuplicateIdTest);
  CPPUNIT_TEST(testInMemoryDuplicateReturnsExistingId);
  CPPUNIT_TEST(testInMemoryDuplicateUnderDifferentNameReturnsSameId);
  CPPUNIT_TEST(testInMemoryDuplicateAcrossAllFormats);
  CPPUNIT_TEST(testStreamedDuplicateReturnsExistingId);
  CPPUNIT_TEST(testStreamedDuplicateRepeatedlyStable);
  CPPUNIT_TEST(testDuplicateAfterValidateOnlyStillOkThenDuplicate);
  CPPUNIT_TEST(testAddImageBlobDuplicateReturnsExistingId);
  CPPUNIT_TEST(testAaeSidecarDuplicateReturnsId);
  CPPUNIT_TEST(testDuplicateDoesNotChangeCount);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_dir;

  config::AppConfig cfgFor(const std::string& name) {
    fs::path base = m_dir / name;
    fs::create_directories(base / "storage");
    config::AppConfig cfg;
    cfg.targets.push_back({base / "storage", base / "imager.db"});
    for (const char* e : {"jpg", "png", "heic", "nef", "mov", "aae"}) {
      cfg.fileSizeLimits[e] = 500 * MB;
    }
    return cfg;
  }

public:
  void setUp() override {
    static std::atomic<int> n{0};
    m_dir = fs::temp_directory_path() / ("imager_dupid_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
    fs::create_directories(m_dir);
  }
  void tearDown() override {
    std::error_code ec;
    fs::remove_all(m_dir, ec);
  }

  void testInMemoryDuplicateReturnsExistingId() {
    Imager img(cfgFor("a"));
    fs::path p = m_dir / "a.jpg";
    spit(p, slurp(JPEG_FIXTURE));
    CPPUNIT_ASSERT(fs::file_size(p) <= 8 * MB);
    auto first = img.addFile(p, "a.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(first.message, ErrorCode::Ok, first.code);
    CPPUNIT_ASSERT(isHex64(first.id));
    auto dup = img.addFile(p, "a.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
    CPPUNIT_ASSERT_MESSAGE("dup id is 64 lowercase hex: '" + dup.id + "'", isHex64(dup.id));
    CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
    CPPUNIT_ASSERT_EQUAL(img.hashOnlyFile(p), dup.id);
  }

  void testInMemoryDuplicateUnderDifferentNameReturnsSameId() {
    Imager img(cfgFor("b"));
    fs::path p = m_dir / "b.jpg";
    spit(p, slurp(JPEG_FIXTURE));
    auto first = img.addFile(p, "one.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, first.code);
    auto dup = img.addFile(p, "two.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
    CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
  }

  void testInMemoryDuplicateAcrossAllFormats() {
    struct F {
      const char* ext;
      std::string path;
    };
    for (const F& f : {F{"heic", HEIC_FIXTURES_DIR "/valid.heic"}, F{"nef", NEF_FIXTURES_DIR "/valid.nef"},
                       F{"mov", MOV_FIXTURES_DIR "/valid.mov"}}) {
      Imager img(cfgFor(std::string("fmt_") + f.ext));
      Bytes b = slurp(f.path);
      if (b.size() > 8 * MB) {
        continue; // exercised by the streamed-path tests
      }
      fs::path p = m_dir / (std::string("x.") + f.ext);
      spit(p, b);
      auto first = img.addFile(p, p.filename().string());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(std::string(f.ext) + ": " + first.message, ErrorCode::Ok, first.code);
      auto dup = img.addFile(p, p.filename().string());
      CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
      CPPUNIT_ASSERT_MESSAGE(std::string(f.ext) + " dup id: '" + dup.id + "'", isHex64(dup.id));
      CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
    }
  }

  /// valid.heic + zero padding to 9 MB (> STREAMING_THRESHOLD) — accepted, goes through the stream path.
  Bytes bigHeic() {
    Bytes b = slurp(HEIC_FIXTURES_DIR "/valid.heic");
    b.resize(9 * MB, 0);
    return b;
  }

  void testStreamedDuplicateReturnsExistingId() {
    Imager img(cfgFor("c"));
    fs::path p = m_dir / "big.heic";
    spit(p, bigHeic());
    CPPUNIT_ASSERT(fs::file_size(p) > 8 * MB);
    auto first = img.addFile(p, "big.heic");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(first.message, ErrorCode::Ok, first.code);
    CPPUNIT_ASSERT(isHex64(first.id));
    auto dup = img.addFile(p, "big.heic");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
    CPPUNIT_ASSERT_MESSAGE("streamed dup id: '" + dup.id + "'", isHex64(dup.id));
    CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
    CPPUNIT_ASSERT_EQUAL(img.hashOnlyFile(p), dup.id);
  }

  void testStreamedDuplicateRepeatedlyStable() {
    Imager img(cfgFor("d"));
    fs::path p = m_dir / "big2.heic";
    spit(p, bigHeic());
    auto first = img.addFile(p, "big2.heic");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, first.code);
    for (int i = 0; i < 3; ++i) {
      auto dup = img.addFile(p, "again" + std::to_string(i) + ".heic");
      CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
      CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
    }
  }

  void testDuplicateAfterValidateOnlyStillOkThenDuplicate() {
    Imager img(cfgFor("e"));
    fs::path p = m_dir / "e.jpg";
    spit(p, slurp(JPEG_FIXTURE));
    auto v = img.validateOnlyFile(p, "e.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, v.code);
    auto a = img.addFile(p, "e.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, a.code);
    CPPUNIT_ASSERT_EQUAL(v.id, a.id);
    auto d = img.addFile(p, "e.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, d.code);
    CPPUNIT_ASSERT_EQUAL(v.id, d.id);
  }

  void testAddImageBlobDuplicateReturnsExistingId() {
    Imager img(cfgFor("f"));
    Bytes b = slurp(JPEG_FIXTURE);
    auto first = img.addImage(Blob::fromVector(Bytes(b)), "f.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, first.code);
    auto dup = img.addImage(Blob::fromVector(Bytes(b)), "f.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
    CPPUNIT_ASSERT_MESSAGE("addImage dup id: '" + dup.id + "'", isHex64(dup.id));
    CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
  }

  /// AAE sidecar added twice (parent present): second add is DuplicateFile with a 64-hex id equal to
  /// the first add's id (spec assumption: sidecar results carry the same id on both calls).
  void testAaeSidecarDuplicateReturnsId() {
    Imager img(cfgFor("g"));
    fs::path dir = m_dir / "in";
    fs::create_directories(dir);
    spit(dir / "IMG_1.jpg", slurp(JPEG_FIXTURE));
    std::string x = aaeXml(1);
    spit(dir / "IMG_1.aae", Bytes(x.begin(), x.end()));
    auto parent = img.addFile(dir / "IMG_1.jpg", "IMG_1.jpg");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, parent.code);
    auto first = img.addFile(dir / "IMG_1.aae", "IMG_1.aae");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(first.message, ErrorCode::Ok, first.code);
    auto dup = img.addFile(dir / "IMG_1.aae", "IMG_1.aae");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(dup.message, ErrorCode::DuplicateFile, dup.code);
    CPPUNIT_ASSERT_MESSAGE("aae dup id: '" + dup.id + "'", isHex64(dup.id));
    CPPUNIT_ASSERT_EQUAL(first.id, dup.id);
  }

  void testDuplicateDoesNotChangeCount() {
    Imager img(cfgFor("h"));
    fs::path p = m_dir / "h.jpg";
    spit(p, slurp(JPEG_FIXTURE));
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, img.addFile(p, "h.jpg").code);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, img.addFile(p, "h.jpg").code);
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(DuplicateIdTest);
