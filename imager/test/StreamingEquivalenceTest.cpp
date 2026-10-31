// StreamingEquivalenceTest.cpp
//
// Guards the "streaming" step: whatever internal path Imager uses for a file
// (whole-file read, stream validators + StreamHasher, any size threshold), the
// file-based API must behave exactly like the in-memory Blob API on the same
// bytes — same ErrorCode, same id (SHA256) — for every validator fixture, valid
// and broken, across all formats.
//
// Oracle: Imager::addImage / validateOnly / hashOnlyBlob (Blob path, untouched
// by the step) on a separate, fresh Imager. Valid fixtures also carry a golden
// SHA256 (sha256sum of the checked-in file) so a shared bug cannot hide.

#include <config/Config.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <imager/Imager.h>
#include <png.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace imager;

namespace {

constexpr uint64_t MB = 1024ULL * 1024ULL;

std::string uniq() {
  static std::atomic<int> counter{0};
  return std::to_string(counter.fetch_add(1)) + "_" + std::to_string(::getpid());
}

using Bytes = std::vector<uint8_t>;

Bytes readFile(const fs::path& p) {
  std::ifstream in(p, std::ios::binary | std::ios::ate);
  if (!in) {
    return {};
  }
  auto sz = static_cast<size_t>(in.tellg());
  in.seekg(0);
  Bytes b(sz);
  if (sz > 0) {
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(sz));
  }
  return b;
}

void writeFile(const fs::path& p, const Bytes& data, uint64_t padTo = 0) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!data.empty()) {
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
  uint64_t written = data.size();
  Bytes zeros(64 * 1024, 0);
  while (written < padTo) {
    auto n = static_cast<size_t>(std::min<uint64_t>(zeros.size(), padTo - written));
    out.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(n));
    written += n;
  }
}

Blob toBlob(Bytes b) {
  return Blob::fromVector(std::move(b));
}

void pngWrite(png_structp p, png_bytep d, png_size_t n) {
  auto* v = static_cast<Bytes*>(png_get_io_ptr(p));
  v->insert(v->end(), d, d + n);
}
void pngFlush(png_structp) {}

/// Valid 8x8 grayscale PNG generated with libpng.
Bytes makePng() {
  Bytes buf;
  png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(p);
  if (setjmp(png_jmpbuf(p))) {
    png_destroy_write_struct(&p, &info);
    return {};
  }
  png_set_write_fn(p, &buf, pngWrite, pngFlush);
  png_set_IHDR(
    p,
    info,
    8,
    8,
    8,
    PNG_COLOR_TYPE_GRAY,
    PNG_INTERLACE_NONE,
    PNG_COMPRESSION_TYPE_DEFAULT,
    PNG_FILTER_TYPE_DEFAULT
  );
  png_write_info(p, info);
  for (int y = 0; y < 8; ++y) {
    uint8_t row[8];
    for (int x = 0; x < 8; ++x) {
      row[x] = static_cast<uint8_t>(x * 30 + y);
    }
    png_write_row(p, row);
  }
  png_write_end(p, info);
  png_destroy_write_struct(&p, &info);
  return buf;
}

Bytes makeAae() {
  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
    "<plist version=\"1.0\"><dict><key>adjustmentFormatVersion</key><integer>1</integer></dict></plist>\n";
  return Bytes(xml.begin(), xml.end());
}

struct Fixture {
  std::string name;       ///< label for failure messages
  std::string ext;        ///< file extension used as the display filename
  Bytes bytes;
  std::string goldenId;   ///< expected SHA256 of the bytes when known ("" = derive from oracle only)
  bool expectValid;       ///< true: must be accepted by every path
};

Bytes truncated(const Bytes& b, size_t n) {
  return Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(std::min(n, b.size())));
}

Bytes corrupted(Bytes b, size_t offset, uint8_t xorv) {
  if (offset < b.size()) {
    b[offset] ^= xorv;
  }
  return b;
}

Bytes garbage(size_t n) {
  Bytes b(n);
  uint32_t s = 12345;
  for (auto& x : b) {
    s = s * 1664525u + 1013904223u;
    x = static_cast<uint8_t>(s >> 24);
  }
  return b;
}

/// One side of a comparison: code + id (+ message for diagnostics).
struct Outcome {
  ErrorCode code;
  std::string id;
  std::string msg;
};

} // namespace

class StreamingEquivalenceTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(StreamingEquivalenceTest);
  CPPUNIT_TEST(testValidFixturesAllPathsAcceptWithGoldenId);
  CPPUNIT_TEST(testBrokenFixturesFileApiMatchesBlobApi);
  CPPUNIT_TEST(testBrokenFixturesNeverAccepted);
  CPPUNIT_TEST(testHashOnlyFileMatchesBlobForAnyBytes);
  CPPUNIT_TEST(testEmptyAndTinyFiles);
  CPPUNIT_TEST(testWrongAndUnknownExtension);
  CPPUNIT_TEST(testDuplicateAfterAddFile);
  CPPUNIT_TEST(testDeleteFileMatchesAddedId);
  CPPUNIT_TEST(testSizeLimitBoundaryUnchanged);
  CPPUNIT_TEST(testAaeAboveInMemoryThresholdTooLarge);
  CPPUNIT_TEST(testPaddedMovAroundCandidateThresholds);
  CPPUNIT_TEST(testPaddedJpegAroundCandidateThresholds);
  CPPUNIT_TEST(testPaddedMovAroundOldThreshold);
  CPPUNIT_TEST(testAddFileReportsPipelineStages);
  CPPUNIT_TEST(testMissingFileReported);
  CPPUNIT_TEST(testAaeSidecarOwnPathPairsWithParent);
  CPPUNIT_TEST(testAaeOrphanRelocatedOnParentArrival);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_base;
  fs::path m_files;
  int m_n = 0;

  struct Store {
    fs::path base;
    std::unique_ptr<Imager> img;
  };

  config::AppConfig makeCfg(const fs::path& base) {
    fs::create_directories(base / "storage");
    config::AppConfig cfg;
    cfg.targets.push_back({base / "storage", base / "imager.db"});
    cfg.fileSizeLimits["jpg"] = 250 * MB;
    cfg.fileSizeLimits["jpeg"] = 250 * MB;
    cfg.fileSizeLimits["png"] = 500 * MB;
    cfg.fileSizeLimits["heic"] = 250 * MB;
    cfg.fileSizeLimits["heif"] = 250 * MB;
    cfg.fileSizeLimits["nef"] = 500 * MB;
    cfg.fileSizeLimits["aae"] = 1 * MB;
    cfg.fileSizeLimits["mov"] = 100 * 1024 * MB;
    cfg.fileSizeLimits["mp4"] = 100 * 1024 * MB;
    return cfg;
  }

  /// Fresh, empty Imager (own storage + DB) so operations never see each other's data.
  Store freshStore() {
    Store s;
    s.base = m_base / ("store" + std::to_string(m_n++));
    s.img = std::make_unique<Imager>(makeCfg(s.base));
    return s;
  }

  std::vector<Fixture> validFixtures() {
    std::vector<Fixture> v;
    auto add = [&](const std::string& name, const std::string& ext, const fs::path& p, const std::string& gid) {
      Bytes b = readFile(p);
      CPPUNIT_ASSERT_MESSAGE("fixture missing/empty: " + p.string(), !b.empty());
      v.push_back({name, ext, std::move(b), gid, true});
    };
    add("jpeg", "jpg", fs::path(JPEG_FIXTURE), "b7ea0b521397c20176b809eb19fd4b51ea83368a3d8becb89ff430dcf8ea4e57");
    add("heic", "heic", fs::path(HEIC_FIXTURES_DIR "/valid.heic"),
        "b053229721127cf8a708d27b510aa00d8f42286dc5c0d0b6fa89f122bf3e820f");
    add("nef", "nef", fs::path(NEF_FIXTURES_DIR "/valid.nef"),
        "d32f209a0b32566da06a9411dbca34a4b368cea11089701b6fe80e63c1d82806");
    add("mov", "mov", fs::path(MOV_FIXTURES_DIR "/valid.mov"),
        "89e8532d5cefeed65fb546accc6508ca97bc9a4de963b450f968b2740a58fc86");
    add("mov-as-mp4", "mp4", fs::path(MOV_FIXTURES_DIR "/valid.mov"),
        "89e8532d5cefeed65fb546accc6508ca97bc9a4de963b450f968b2740a58fc86");
    add("aae", "aae", fs::path(AAE_FIXTURES_DIR "/valid.aae"),
        "c03752eb4013e64b3865c283e4523332c4f98b74f0594adb583a8677e2742760");
    v.push_back({"png(generated)", "png", makePng(), "", true});
    CPPUNIT_ASSERT(!v.back().bytes.empty());
    return v;
  }

  std::vector<Fixture> brokenFixtures() {
    std::vector<Fixture> out;
    for (const auto& f : validFixtures()) {
      if (f.name == "mov-as-mp4") {
        continue;
      }
      const Bytes& b = f.bytes;
      out.push_back({f.name + ":truncated-half", f.ext, truncated(b, b.size() / 2), "", false});
      out.push_back({f.name + ":truncated-to-16", f.ext, truncated(b, 16), "", false});
      out.push_back({f.name + ":truncated-by-1", f.ext, truncated(b, b.size() - 1), "", false});
      out.push_back({f.name + ":header-corrupt", f.ext, corrupted(b, 4, 0xFF), "", false});
      out.push_back({f.name + ":garbage", f.ext, garbage(b.size() < 4096 ? 4096 : 100000), "", false});
      out.push_back({f.name + ":zeros", f.ext, Bytes(8192, 0), "", false});
    }
    return out;
  }

  Outcome viaAddFile(const Bytes& b, const std::string& ext, const std::string& stem = "f") {
    auto s = freshStore();
    fs::path p = m_files / (stem + std::to_string(m_n++) + "." + ext);
    writeFile(p, b);
    auto r = s.img->addFile(p, stem + "." + ext);
    fs::remove(p);
    return {r.code, r.id, r.message};
  }
  Outcome viaAddImage(const Bytes& b, const std::string& ext, const std::string& stem = "f") {
    auto s = freshStore();
    auto r = s.img->addImage(toBlob(b), stem + "." + ext);
    return {r.code, r.id, r.message};
  }
  Outcome viaValidateOnlyFile(const Bytes& b, const std::string& ext, const std::string& stem = "f") {
    auto s = freshStore();
    fs::path p = m_files / (stem + std::to_string(m_n++) + "." + ext);
    writeFile(p, b);
    auto r = s.img->validateOnlyFile(p, stem + "." + ext);
    fs::remove(p);
    return {r.code, r.id, r.message};
  }
  Outcome viaValidateOnlyBlob(const Bytes& b, const std::string& ext, const std::string& stem = "f") {
    auto s = freshStore();
    auto r = s.img->validateOnly(toBlob(b), stem + "." + ext);
    return {r.code, r.id, r.message};
  }

  static std::string str(ErrorCode c) {
    return std::to_string(static_cast<int>(c));
  }

  void assertSame(const std::string& what, const Outcome& oracle, const Outcome& got) {
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
      what + ": code differs from Blob oracle (oracle msg='" + oracle.msg + "', got msg='" + got.msg + "')",
      str(oracle.code),
      str(got.code)
    );
    CPPUNIT_ASSERT_EQUAL_MESSAGE(what + ": id differs from Blob oracle", oracle.id, got.id);
    if (got.code != ErrorCode::Ok) {
      CPPUNIT_ASSERT_MESSAGE(what + ": rejection must carry a message", !got.msg.empty());
    }
  }

  // Compare all three file operations against the Blob oracle for one input.
  void compareAll(const std::string& label, const Bytes& b, const std::string& ext) {
    Outcome oAdd = viaAddImage(b, ext);
    Outcome oVal = viaValidateOnlyBlob(b, ext);
    assertSame(label + " addFile", oAdd, viaAddFile(b, ext));
    assertSame(label + " validateOnlyFile", oVal, viaValidateOnlyFile(b, ext));

    // hashOnlyFile: pure SHA256 regardless of validity.
    auto s = freshStore();
    fs::path p = m_files / ("h" + std::to_string(m_n++) + "." + ext);
    writeFile(p, b);
    std::string fileHash = s.img->hashOnlyFile(p);
    fs::remove(p);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " hashOnlyFile vs hashOnlyBlob", s.img->hashOnlyBlob(toBlob(b)), fileHash);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " hashOnlyFile length", size_t(64), fileHash.size());
  }

public:
  void setUp() override {
    m_base = fs::temp_directory_path() / ("imager_streq_" + uniq());
    m_files = m_base / "in";
    fs::create_directories(m_files);
  }
  void tearDown() override {
    std::error_code ec;
    fs::remove_all(m_base, ec);
  }

  /// Every valid fixture: accepted by addFile / validateOnlyFile with the golden id.
  void testValidFixturesAllPathsAcceptWithGoldenId() {
    for (const auto& f : validFixtures()) {
      Outcome a = viaAddFile(f.bytes, f.ext);
      Outcome v = viaValidateOnlyFile(f.bytes, f.ext);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " addFile accepted: " + a.msg, str(ErrorCode::Ok), str(a.code));
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " validateOnlyFile accepted: " + v.msg, str(ErrorCode::Ok), str(v.code));
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " addFile/validateOnlyFile ids agree", a.id, v.id);
      if (!f.goldenId.empty()) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " golden id (addFile)", f.goldenId, a.id);
      }
      compareAll(f.name, f.bytes, f.ext);

      // hashOnlyFile on the very same bytes yields the same id
      auto s = freshStore();
      fs::path p = m_files / ("g" + std::to_string(m_n++) + "." + f.ext);
      writeFile(p, f.bytes);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " hashOnlyFile id", a.id, s.img->hashOnlyFile(p));
    }
  }

  /// Every broken variant: file API == Blob API (code and id).
  void testBrokenFixturesFileApiMatchesBlobApi() {
    for (const auto& f : brokenFixtures()) {
      compareAll(f.name, f.bytes, f.ext);
    }
  }

  /// Truncated / zeroed / garbage content must be rejected (not Ok) on the file API.
  /// (header-corrupt is excluded: a flipped byte may still be a decodable file —
  /// it is covered by the equivalence test above.)
  void testBrokenFixturesNeverAccepted() {
    for (const auto& f : brokenFixtures()) {
      if (f.name.find("header-corrupt") != std::string::npos || f.name.find("truncated-by-1") != std::string::npos) {
        continue; // may legitimately still validate for lenient decoders
      }
      if (f.name == "heic:truncated-half") {
        continue; // libheif accepts it on every path (Blob API too); equivalence test covers it
      }
      Outcome a = viaAddFile(f.bytes, f.ext);
      Outcome v = viaValidateOnlyFile(f.bytes, f.ext);
      CPPUNIT_ASSERT_MESSAGE(f.name + " addFile must reject", a.code != ErrorCode::Ok);
      CPPUNIT_ASSERT_MESSAGE(f.name + " validateOnlyFile must reject", v.code != ErrorCode::Ok);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " BrokenFile (addFile)", str(ErrorCode::BrokenFile), str(a.code));
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " BrokenFile (validateOnlyFile)", str(ErrorCode::BrokenFile), str(v.code));
    }
  }

  /// hashOnlyFile does not validate: any bytes, any extension.
  void testHashOnlyFileMatchesBlobForAnyBytes() {
    auto s = freshStore();
    for (size_t n : {size_t(1), size_t(63), size_t(64), size_t(65), size_t(4095), size_t(4096), size_t(4097),
                     size_t(65535), size_t(65536), size_t(65537), size_t(1000003)}) {
      Bytes b = garbage(n);
      fs::path p = m_files / ("any" + std::to_string(n) + ".bin");
      writeFile(p, b);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "hashOnlyFile size " + std::to_string(n), s.img->hashOnlyBlob(toBlob(b)), s.img->hashOnlyFile(p)
      );
    }
  }

  /// 0-byte and 1-byte files with every supported extension.
  void testEmptyAndTinyFiles() {
    for (const char* ext : {"jpg", "png", "heic", "nef", "mov", "aae"}) {
      compareAll(std::string("empty.") + ext, Bytes{}, ext);
      compareAll(std::string("one-byte.") + ext, Bytes{0x00}, ext);
    }
  }

  /// Valid JPEG bytes named .png / .mov / .xyz / no extension; case-insensitive ext.
  void testWrongAndUnknownExtension() {
    Bytes jpg = validFixtures().front().bytes;
    for (const char* ext : {"png", "mov", "heic", "aae", "xyz", "JPG", "Jpeg"}) {
      compareAll(std::string("jpeg-bytes-as.") + ext, jpg, ext);
    }
    Outcome u = viaAddFile(jpg, "xyz");
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::UnsupportedFormat), str(u.code));
    Outcome up = viaAddFile(jpg, "JPG");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("upper-case extension accepted: " + up.msg, str(ErrorCode::Ok), str(up.code));
  }

  /// addFile twice -> DuplicateFile with the same id; validateOnlyFile sees the duplicate too.
  void testDuplicateAfterAddFile() {
    for (const auto& f : validFixtures()) {
      if (f.name == "aae" || f.name == "mov-as-mp4") {
        continue; // sidecar/ext variants covered elsewhere
      }
      auto s = freshStore();
      fs::path p = m_files / ("d" + std::to_string(m_n++) + "." + f.ext);
      writeFile(p, f.bytes);
      auto r1 = s.img->addFile(p, "one." + f.ext);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " first add: " + r1.message, str(ErrorCode::Ok), str(r1.code));
      auto r2 = s.img->addFile(p, "two." + f.ext);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " second add", str(ErrorCode::DuplicateFile), str(r2.code));
      // addFile reports no id on DuplicateFile (pre-existing behaviour); validateOnlyFile does.
      auto v = s.img->validateOnlyFile(p, "three." + f.ext);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " validateOnlyFile sees duplicate", str(ErrorCode::DuplicateFile), str(v.code));
      CPPUNIT_ASSERT_EQUAL(r1.id, v.id);
      // Storage content is byte-identical to the source.
      auto stored = s.img->getImageData(r1.id);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " stored size", f.bytes.size(), static_cast<size_t>(stored.size()));
      CPPUNIT_ASSERT_MESSAGE(
        f.name + " stored bytes identical",
        std::equal(f.bytes.begin(), f.bytes.end(), stored.data())
      );
    }
  }

  /// deleteFile(path) removes what addFile(path) stored, returning the same id.
  void testDeleteFileMatchesAddedId() {
    for (const auto& f : validFixtures()) {
      if (f.name == "aae" || f.name == "mov-as-mp4") {
        continue;
      }
      auto s = freshStore();
      fs::path p = m_files / ("x" + std::to_string(m_n++) + "." + f.ext);
      writeFile(p, f.bytes);
      auto r = s.img->addFile(p, "x." + f.ext);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " add: " + r.message, str(ErrorCode::Ok), str(r.code));
      CPPUNIT_ASSERT_EQUAL(uint64_t(1), s.img->imageCount());
      auto d = s.img->deleteFile(p);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " deleteFile", str(ErrorCode::Ok), str(d.code));
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " deleteFile id", r.id, d.id);
      CPPUNIT_ASSERT_EQUAL(uint64_t(0), s.img->imageCount());
      auto d2 = s.img->deleteFile(p);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.name + " second deleteFile", str(ErrorCode::FileNotFound), str(d2.code));
      CPPUNIT_ASSERT_EQUAL(r.id, d2.id);
    }
  }

  /// Per-format size limits are still enforced exactly at limit / limit+1 (AAE: 1 MB).
  void testSizeLimitBoundaryUnchanged() {
    Bytes aae = makeAae();
    // exactly at the limit: padded with zeros -> not TooLarge (may be Broken, never TooLarge)
    Outcome at = viaAddFile(Bytes(1 * MB, 0), "aae");
    CPPUNIT_ASSERT_MESSAGE("AAE at limit must not be TooLarge", at.code != ErrorCode::TooLarge);
    Outcome over = viaAddFile(Bytes(1 * MB + 1, 0), "aae");
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::TooLarge), str(over.code));
    Outcome vover = viaValidateOnlyFile(Bytes(1 * MB + 1, 0), "aae");
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::TooLarge), str(vover.code));
    // a real AAE well under the limit still passes
    Outcome ok = viaAddFile(aae, "aae");
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::Ok), str(ok.code));
  }

  /// Valid MOV with trailing zero padding at sizes around every plausible
  /// streaming threshold (±1 byte): result and id must equal the Blob path.
  void testPaddedMovAroundCandidateThresholds() {
    Bytes mov = readFile(fs::path(MOV_FIXTURES_DIR "/valid.mov"));
    CPPUNIT_ASSERT(!mov.empty());
    for (uint64_t t : {uint64_t{64 * 1024}, 1 * MB, 4 * MB, 8 * MB, 12 * MB, 16 * MB, 32 * MB, 64 * MB}) {
      for (int64_t d : {-1, 0, 1}) {
        uint64_t size = t + static_cast<uint64_t>(d);
        Bytes b = mov;
        b.resize(size, 0);
        std::string label = "mov padded to " + std::to_string(size);
        Outcome oracle = viaAddImage(b, "mov");
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " oracle accepts: " + oracle.msg, str(ErrorCode::Ok), str(oracle.code));
        assertSame(label + " addFile", oracle, viaAddFile(b, "mov"));
        assertSame(label + " validateOnlyFile", viaValidateOnlyBlob(b, "mov"), viaValidateOnlyFile(b, "mov"));
        auto s = freshStore();
        fs::path p = m_files / ("pm" + std::to_string(m_n++) + ".mov");
        writeFile(p, b);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " hashOnlyFile", oracle.id, s.img->hashOnlyFile(p));
        fs::remove(p);
      }
    }
  }

  /// Same for JPEG (trailing zeros after EOI): whatever the Blob path says, files agree.
  void testPaddedJpegAroundCandidateThresholds() {
    Bytes jpg = readFile(fs::path(JPEG_FIXTURE));
    CPPUNIT_ASSERT(!jpg.empty());
    for (uint64_t t : {uint64_t{64 * 1024}, 1 * MB, 4 * MB, 8 * MB, 12 * MB, 16 * MB, 32 * MB}) {
      for (int64_t d : {-1, 0, 1}) {
        uint64_t size = t + static_cast<uint64_t>(d);
        if (size < jpg.size()) {
          continue;
        }
        Bytes b = jpg;
        b.resize(size, 0);
        compareAll("jpeg padded to " + std::to_string(size), b, "jpg");
      }
    }
  }

  /// .aae always takes the in-memory path: above the 8 MB threshold it is TooLarge even
  /// when the configured limit would allow more; at 8 MB it is not TooLarge.
  void testAaeAboveInMemoryThresholdTooLarge() {
    auto base = m_base / "aae8";
    auto cfg = makeCfg(base);
    cfg.fileSizeLimits["aae"] = 64 * MB;
    Imager img(cfg);
    fs::path p = m_files / "big8.aae";
    writeFile(p, Bytes{}, 8 * MB);
    auto at = img.validateOnlyFile(p, "big8.aae");
    CPPUNIT_ASSERT_MESSAGE("AAE of exactly 8 MB is not TooLarge", at.code != ErrorCode::TooLarge);
    writeFile(p, Bytes{}, 8 * MB + 1);
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::TooLarge), str(img.validateOnlyFile(p, "big8.aae").code));
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::TooLarge), str(img.addFile(p, "big8.aae").code));
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
  }

  /// Around the historical 256 MiB threshold: the three file ops must agree
  /// with each other and accept (the Blob oracle would need 256 MB of RAM twice).
  void testPaddedMovAroundOldThreshold() {
    Bytes mov = readFile(fs::path(MOV_FIXTURES_DIR "/valid.mov"));
    for (int64_t d : {-1, 0, 1}) {
      uint64_t size = 256 * MB + static_cast<uint64_t>(d);
      fs::path p = m_files / ("big" + std::to_string(m_n++) + ".mov");
      writeFile(p, mov, size);
      CPPUNIT_ASSERT_EQUAL(size, static_cast<uint64_t>(fs::file_size(p)));
      auto s = freshStore();
      std::string label = "mov " + std::to_string(size);
      auto v = s.img->validateOnlyFile(p, "big.mov");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " validateOnlyFile: " + v.message, str(ErrorCode::Ok), str(v.code));
      std::string h = s.img->hashOnlyFile(p);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " hashOnlyFile == validateOnlyFile id", v.id, h);
      auto a = s.img->addFile(p, "big.mov");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " addFile: " + a.message, str(ErrorCode::Ok), str(a.code));
      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " addFile id", v.id, a.id);
      auto d2 = s.img->deleteFile(p);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " deleteFile id", v.id, d2.id);
      CPPUNIT_ASSERT_EQUAL(str(ErrorCode::Ok), str(d2.code));
      fs::remove(p);
    }
  }

  /// StageCallback still sees Reading/Validating/Hashing/.../InsertingDb for a stored file.
  void testAddFileReportsPipelineStages() {
    auto s = freshStore();
    Bytes jpg = readFile(fs::path(JPEG_FIXTURE));
    fs::path p = m_files / "stages.jpg";
    writeFile(p, jpg);
    std::set<ProcessingStage> seen;
    std::mutex mu;
    auto r = s.img->addFile(p, "stages.jpg", [&](ProcessingStage st) {
      std::lock_guard lk(mu);
      seen.insert(st);
    });
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::Ok), str(r.code));
    // In-memory path reports Validating (Hashing is only reported by the streamed path).
    for (auto st : {ProcessingStage::Validating, ProcessingStage::WritingStorage, ProcessingStage::InsertingDb}) {
      CPPUNIT_ASSERT_MESSAGE("stage " + std::to_string(static_cast<int>(st)) + " reported", seen.count(st) == 1);
    }

    // hashOnlyFile / deleteFile also report Hashing.
    std::set<ProcessingStage> hs;
    s.img->hashOnlyFile(p, [&](ProcessingStage st) {
      std::lock_guard lk(mu);
      hs.insert(st);
    });
    CPPUNIT_ASSERT(hs.count(ProcessingStage::Hashing) == 1);

    // A streamed file (> 8 MB) reports Hashing on addFile.
    Bytes mov = readFile(fs::path(MOV_FIXTURES_DIR "/valid.mov"));
    fs::path big = m_files / "stages9.mov";
    writeFile(big, mov, 9 * MB);
    std::set<ProcessingStage> bs;
    auto rb = s.img->addFile(big, "stages9.mov", [&](ProcessingStage st) {
      std::lock_guard lk(mu);
      bs.insert(st);
    });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("streamed add: " + rb.message, str(ErrorCode::Ok), str(rb.code));
    CPPUNIT_ASSERT(bs.count(ProcessingStage::Hashing) == 1);
    CPPUNIT_ASSERT(bs.count(ProcessingStage::WritingStorage) == 1);
    CPPUNIT_ASSERT(bs.count(ProcessingStage::InsertingDb) == 1);
  }

  /// A path that does not exist: no crash; non-Ok / empty hash, no state change.
  void testMissingFileReported() {
    auto s = freshStore();
    fs::path p = m_files / "nope.jpg";
    auto a = s.img->addFile(p, "nope.jpg");
    CPPUNIT_ASSERT(a.code != ErrorCode::Ok);
    auto v = s.img->validateOnlyFile(p, "nope.jpg");
    CPPUNIT_ASSERT(v.code != ErrorCode::Ok);
    CPPUNIT_ASSERT(s.img->hashOnlyFile(p).empty());
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), s.img->imageCount());
  }

  /// An AAE added through addFile after its parent is stored under the parent's
  /// hash prefix, and its own bytes are retrievable/identical.
  void testAaeSidecarOwnPathPairsWithParent() {
    auto s = freshStore();
    fs::path dir = m_files / "album";
    fs::create_directories(dir);
    Bytes jpg = readFile(fs::path(JPEG_FIXTURE));
    Bytes aae = makeAae();
    writeFile(dir / "IMG_1234.jpg", jpg);
    writeFile(dir / "IMG_1234.aae", aae);
    auto rj = s.img->addFile(dir / "IMG_1234.jpg", "album/IMG_1234.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("parent: " + rj.message, str(ErrorCode::Ok), str(rj.code));
    auto ra = s.img->addFile(dir / "IMG_1234.aae", "album/IMG_1234.aae");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("sidecar: " + ra.message, str(ErrorCode::Ok), str(ra.code));
    CPPUNIT_ASSERT(rj.id != ra.id);

    // Same id/behaviour as the Blob API on a second store.
    auto o = freshStore();
    auto oj = o.img->addImage(toBlob(jpg), "album/IMG_1234.jpg");
    auto oa = o.img->addImage(toBlob(aae), "album/IMG_1234.aae");
    CPPUNIT_ASSERT_EQUAL(oj.id, rj.id);
    CPPUNIT_ASSERT_EQUAL(oa.id, ra.id);
    CPPUNIT_ASSERT_EQUAL(str(oa.code), str(ra.code));

    auto data = s.img->getImageData(ra.id);
    CPPUNIT_ASSERT_EQUAL(aae.size(), static_cast<size_t>(data.size()));
    CPPUNIT_ASSERT(std::equal(aae.begin(), aae.end(), data.data()));

    // Stored under the parent's hash prefix on disk.
    bool underParent = false;
    for (auto& e : fs::recursive_directory_iterator(s.base / "storage")) {
      if (e.path().extension() == ".aae" && e.path().filename().string().rfind(rj.id, 0) == 0) {
        underParent = true;
      }
    }
    CPPUNIT_ASSERT_MESSAGE("sidecar stored with parent's hash prefix", underParent);

    // Second add of the sidecar -> DuplicateFile; deleting the parent cascades.
    auto dup = s.img->addFile(dir / "IMG_1234.aae", "album/IMG_1234.aae");
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::DuplicateFile), str(dup.code));
    CPPUNIT_ASSERT_EQUAL(str(ErrorCode::Ok), str(s.img->deleteImage(rj.id)));
    for (auto& e : fs::recursive_directory_iterator(s.base / "storage")) {
      CPPUNIT_ASSERT_MESSAGE("no leftover files after cascade: " + e.path().string(), !e.is_regular_file());
    }
  }

  /// Orphan AAE (via addFile) is relocated when the parent arrives (via addFile).
  void testAaeOrphanRelocatedOnParentArrival() {
    auto s = freshStore();
    fs::path dir = m_files / "orph";
    fs::create_directories(dir);
    Bytes jpg = readFile(fs::path(JPEG_FIXTURE));
    Bytes aae = makeAae();
    writeFile(dir / "IMG_9.aae", aae);
    writeFile(dir / "IMG_9.jpg", jpg);
    auto ra = s.img->addFile(dir / "IMG_9.aae", "orph/IMG_9.aae");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("orphan: " + ra.message, str(ErrorCode::Ok), str(ra.code));
    auto rj = s.img->addFile(dir / "IMG_9.jpg", "orph/IMG_9.jpg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("parent: " + rj.message, str(ErrorCode::Ok), str(rj.code));
    auto data = s.img->getImageData(ra.id);
    CPPUNIT_ASSERT_EQUAL(aae.size(), static_cast<size_t>(data.size()));
    bool underParent = false;
    for (auto& e : fs::recursive_directory_iterator(s.base / "storage")) {
      if (e.path().extension() == ".aae" && e.path().filename().string().rfind(rj.id, 0) == 0) {
        underParent = true;
      }
    }
    CPPUNIT_ASSERT_MESSAGE("orphan relocated under parent's hash", underParent);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(StreamingEquivalenceTest);
