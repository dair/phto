// StreamValidatorEquivalenceTest.cpp
//
// Direct per-format check, independent of the Imager streaming threshold: every repo
// fixture (valid and many broken variants) is run through the format's IStreamValidator
// (file based) and its IValidator (Blob based); both must agree on accept/reject.
// Real files > 8 MB (NEF raw 20-60 MB, big PNG) take the stream path, but all repo
// fixtures are < 8 MB, so this is the only coverage of those stream validators.
// Also: a generated > 8 MB noise PNG through addFile / validateOnlyFile.

#include <config/Config.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <imager/Imager.h>
#include <imager/Validators.h>
#include <png.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
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

void pngWrite(png_structp p, png_bytep d, png_size_t n) {
  auto* v = static_cast<Bytes*>(png_get_io_ptr(p));
  v->insert(v->end(), d, d + n);
}
void pngFlush(png_structp) {}

/// w x h RGB PNG of pseudo-random noise (compresses poorly), fast zlib level.
Bytes makeNoisePng(int w, int h, uint32_t seed = 1, bool interlaced = false) {
  Bytes buf;
  png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(p);
  if (setjmp(png_jmpbuf(p))) {
    png_destroy_write_struct(&p, &info);
    return {};
  }
  png_set_write_fn(p, &buf, pngWrite, pngFlush);
  png_set_compression_level(p, 1);
  png_set_IHDR(p, info, static_cast<png_uint_32>(w), static_cast<png_uint_32>(h), 8, PNG_COLOR_TYPE_RGB,
               interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(p, info);
  const int passes = interlaced ? png_set_interlace_handling(p) : 1;
  Bytes img(static_cast<size_t>(w) * 3 * static_cast<size_t>(h));
  uint32_t s = seed;
  for (auto& x : img) {
    s = s * 1664525u + 1013904223u;
    x = static_cast<uint8_t>(s >> 24);
  }
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < h; ++y) {
      png_write_row(p, img.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 3);
    }
  }
  png_write_end(p, info);
  png_destroy_write_struct(&p, &info);
  return buf;
}

Bytes noise(size_t n) {
  Bytes b(n);
  uint32_t s = 777;
  for (auto& x : b) {
    s = s * 1664525u + 1013904223u;
    x = static_cast<uint8_t>(s >> 24);
  }
  return b;
}

struct Format {
  std::string ext; ///< with leading dot
  std::unique_ptr<validation::IValidator> blobV;
  std::unique_ptr<validation::IStreamValidator> streamV;
  Bytes valid;
};

} // namespace

class StreamValidatorEquivalenceTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(StreamValidatorEquivalenceTest);
  CPPUNIT_TEST(testStreamAndBlobValidatorsAgreeOnAllFixtureVariants);
  CPPUNIT_TEST(testValidFixturesAcceptedByStreamValidators);
  CPPUNIT_TEST(testStreamValidatorsRejectTruncatedAndGarbage);
  CPPUNIT_TEST(testStreamValidatorsHandleMissingFile);
  CPPUNIT_TEST(testEveryFormatPaddedAbove8MbAgreesWithBlobApi);
  CPPUNIT_TEST(testInterlacedPngSmallAcceptedByBothValidators);
  CPPUNIT_TEST(testInterlacedPngLargeAcceptedByBothValidatorsAndAddFile);
  CPPUNIT_TEST(testInterlacedPngTruncatedRejected);
  CPPUNIT_TEST(testLargePngAcceptedAndTruncatedRejected);
  CPPUNIT_TEST(testLargePngCorruptedStreamAndBlobAgree);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_dir;

  std::vector<Format> formats() {
    std::vector<Format> v;
    auto add = [&](const char* ext, std::unique_ptr<validation::IValidator> b,
                   std::unique_ptr<validation::IStreamValidator> s, Bytes data) {
      CPPUNIT_ASSERT_MESSAGE(std::string("fixture empty for ") + ext, !data.empty());
      CPPUNIT_ASSERT(b->supportsExtension(ext));
      CPPUNIT_ASSERT(s->supportsExtension(ext));
      v.push_back({ext, std::move(b), std::move(s), std::move(data)});
    };
    add(".jpg", createJpegValidator(), createJpegStreamValidator(), slurp(JPEG_FIXTURE));
    add(".png", createPngValidator(), createPngStreamValidator(), makeNoisePng(64, 64));
    add(".heic", createHeicValidator(), createHeicStreamValidator(), slurp(HEIC_FIXTURES_DIR "/valid.heic"));
    add(".nef", createNefValidator(), createNefStreamValidator(), slurp(NEF_FIXTURES_DIR "/valid.nef"));
    add(".mov", createMovValidator(), createMovStreamValidator(), slurp(MOV_FIXTURES_DIR "/valid.mov"));
    add(".aae", createAaeValidator(), createAaeStreamValidator(), slurp(AAE_FIXTURES_DIR "/valid.aae"));
    return v;
  }

  /// (label, bytes) variants of a valid file.
  static std::vector<std::pair<std::string, Bytes>> variants(const Bytes& b) {
    std::vector<std::pair<std::string, Bytes>> out;
    out.emplace_back("valid", b);
    auto trunc = [&](const std::string& n, size_t len) {
      out.emplace_back(n, Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(std::min(len, b.size()))));
    };
    trunc("truncated-0", 0);
    trunc("truncated-1", 1);
    trunc("truncated-16", 16);
    trunc("truncated-quarter", b.size() / 4);
    trunc("truncated-half", b.size() / 2);
    trunc("truncated-3quarter", b.size() * 3 / 4);
    trunc("truncated-by-1", b.size() - 1);
    trunc("truncated-by-64", b.size() > 64 ? b.size() - 64 : 0);
    for (size_t off : {size_t(0), size_t(1), size_t(4), size_t(8), b.size() / 8, b.size() / 4, b.size() / 2,
                       b.size() * 3 / 4, b.size() - 8, b.size() - 1}) {
      if (off < b.size()) {
        Bytes c = b;
        c[off] ^= 0xFF;
        out.emplace_back("flip@" + std::to_string(off), std::move(c));
        Bytes z = b;
        z[off] = 0;
        out.emplace_back("zero@" + std::to_string(off), std::move(z));
      }
    }
    Bytes mid = b;
    for (size_t i = b.size() / 2; i < std::min(b.size(), b.size() / 2 + 256); ++i) {
      mid[i] = static_cast<uint8_t>(i * 31);
    }
    out.emplace_back("scrambled-256-mid", std::move(mid));
    Bytes pad = b;
    pad.resize(b.size() + 4096, 0);
    out.emplace_back("trailing-zeros", std::move(pad));
    out.emplace_back("garbage", noise(std::max<size_t>(4096, b.size() / 2)));
    out.emplace_back("zeros", Bytes(4096, 0));
    return out;
  }

public:
  void setUp() override {
    static std::atomic<int> n{0};
    m_dir = fs::temp_directory_path() / ("imager_sveq_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
    fs::create_directories(m_dir);
  }
  void tearDown() override {
    std::error_code ec;
    fs::remove_all(m_dir, ec);
  }

  void testStreamAndBlobValidatorsAgreeOnAllFixtureVariants() {
    for (auto& f : formats()) {
      size_t rejected = 0, accepted = 0;
      for (auto& [label, bytes] : variants(f.valid)) {
        fs::path p = m_dir / ("v" + f.ext);
        spit(p, bytes);
        auto s = f.streamV->validateFile(p);
        auto b = f.blobV->validate(bytes.data(), bytes.size());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
          f.ext + " " + label + ": stream(valid=" + std::to_string(s.valid) + ", '" + s.errorMessage +
            "') vs blob(valid=" + std::to_string(b.valid) + ", '" + b.errorMessage + "')",
          b.valid,
          s.valid
        );
        (s.valid ? accepted : rejected)++;
        if (!s.valid) {
          CPPUNIT_ASSERT_MESSAGE(f.ext + " " + label + ": rejection carries a message", !s.errorMessage.empty());
        }
      }
      CPPUNIT_ASSERT_MESSAGE(f.ext + ": at least the valid file accepted", accepted >= 1);
      CPPUNIT_ASSERT_MESSAGE(f.ext + ": suite is not vacuous (some variants rejected)", rejected >= 3);
    }
  }

  void testValidFixturesAcceptedByStreamValidators() {
    for (auto& f : formats()) {
      fs::path p = m_dir / ("ok" + f.ext);
      spit(p, f.valid);
      auto s = f.streamV->validateFile(p);
      CPPUNIT_ASSERT_MESSAGE(f.ext + " stream accepts valid fixture: " + s.errorMessage, s.valid);
    }
  }

  void testStreamValidatorsRejectTruncatedAndGarbage() {
    for (auto& f : formats()) {
      for (const char* label : {"truncated-0", "truncated-16", "garbage", "zeros"}) {
        for (auto& [l, bytes] : variants(f.valid)) {
          if (l != label) {
            continue;
          }
          fs::path p = m_dir / ("bad" + f.ext);
          spit(p, bytes);
          CPPUNIT_ASSERT_MESSAGE(f.ext + " stream rejects " + l, !f.streamV->validateFile(p).valid);
        }
      }
    }
  }

  void testStreamValidatorsHandleMissingFile() {
    for (auto& f : formats()) {
      auto r = f.streamV->validateFile(m_dir / ("nonexistent" + f.ext));
      CPPUNIT_ASSERT_MESSAGE(f.ext + " missing file rejected", !r.valid);
    }
  }

  static config::AppConfig cfgFor(const fs::path& base) {
    fs::create_directories(base / "storage");
    config::AppConfig cfg;
    cfg.targets.push_back({base / "storage", base / "imager.db"});
    cfg.fileSizeLimits["png"] = 500 * MB;
    return cfg;
  }

  /// Every format's valid fixture padded with zeros to 9 MB (stream path through Imager):
  /// validateOnlyFile and addFile must agree with the Blob API. (.aae > 8 MB is TooLarge by design.)
  void testEveryFormatPaddedAbove8MbAgreesWithBlobApi() {
    int i = 0;
    for (auto& f : formats()) {
      Bytes b = f.valid;
      b.resize(9 * MB, 0);
      auto cfgA = cfgFor(m_dir / ("pa" + std::to_string(i)));
      auto cfgB = cfgFor(m_dir / ("pb" + std::to_string(i)));
      for (auto* c : {&cfgA, &cfgB}) {
        for (const char* e : {"jpg", "heic", "nef", "mov", "aae"}) {
          c->fileSizeLimits[e] = 500 * MB;
        }
      }
      ++i;
      std::string name = "padded" + f.ext;
      fs::path p = m_dir / name;
      spit(p, b);
      Imager file(cfgA);
      Imager blob(cfgB);
      auto fv = file.validateOnlyFile(p, name);
      auto bv = blob.validateOnly(Blob::fromVector(Bytes(b)), name);
      if (f.ext == ".aae") {
        CPPUNIT_ASSERT_EQUAL(ErrorCode::TooLarge, fv.code);
        continue;
      }
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.ext + " validateOnlyFile(file='" + fv.message + "') vs blob('" + bv.message + "')",
                                   bv.code, fv.code);
      CPPUNIT_ASSERT_EQUAL(bv.id, fv.id);
      auto fa = file.addFile(p, name);
      auto ba = blob.addImage(Blob::fromVector(Bytes(b)), name);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(f.ext + " addFile: " + fa.message, ba.code, fa.code);
      CPPUNIT_ASSERT_EQUAL(ba.id, fa.id);
    }
  }

  void checkInterlaced(const Bytes& png, const std::string& label) {
    CPPUNIT_ASSERT_MESSAGE(label + ": generated", !png.empty());
    // sanity: IHDR interlace byte (offset 28) is 1 (Adam7)
    CPPUNIT_ASSERT_EQUAL_MESSAGE(label + ": is Adam7", 1, static_cast<int>(png[28]));
    fs::path p = m_dir / (label + ".png");
    spit(p, png);
    auto b = createPngValidator()->validate(png.data(), png.size());
    auto s = createPngStreamValidator()->validateFile(p);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " blob vs stream agree (blob='" + b.errorMessage + "', stream='" + s.errorMessage + "')", b.valid, s.valid);
    CPPUNIT_ASSERT_MESSAGE(label + " Blob validator accepts valid interlaced PNG: " + b.errorMessage, b.valid);
    CPPUNIT_ASSERT_MESSAGE(label + " stream validator accepts valid interlaced PNG: " + s.errorMessage, s.valid);
    Imager img(cfgFor(m_dir / ("il_" + label)));
    auto v = img.validateOnlyFile(p, label + ".png");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " validateOnlyFile: " + v.message, ErrorCode::Ok, v.code);
    auto a = img.addFile(p, label + ".png");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " addFile: " + a.message, ErrorCode::Ok, a.code);
    CPPUNIT_ASSERT_EQUAL(v.id, a.id);
    CPPUNIT_ASSERT_EQUAL(v.id, img.hashOnlyFile(p));
  }

  void testInterlacedPngSmallAcceptedByBothValidators() {
    checkInterlaced(makeNoisePng(37, 29, 3, true), "il_small");
    checkInterlaced(makeNoisePng(1, 1, 4, true), "il_1x1");
  }

  void testInterlacedPngLargeAcceptedByBothValidatorsAndAddFile() {
    Bytes png = makeNoisePng(2000, 2000, 9, true);
    CPPUNIT_ASSERT_MESSAGE("interlaced PNG is > 8 MB: " + std::to_string(png.size()), png.size() > 8 * MB);
    checkInterlaced(png, "il_large");
  }

  /// A truncated / corrupted Adam7 PNG must still be rejected by both validators (the later
  /// interlace passes must actually be read), and they must agree.
  void testInterlacedPngTruncatedRejected() {
    for (auto dims : {std::pair{64, 64}, std::pair{500, 400}}) {
      Bytes png = makeNoisePng(dims.first, dims.second, 11, true);
      auto bv = createPngValidator();
      auto sv = createPngStreamValidator();
      for (size_t len : {png.size() - 1, png.size() - 12, png.size() * 9 / 10, png.size() * 3 / 4, png.size() / 2}) {
        Bytes t(png.begin(), png.begin() + static_cast<std::ptrdiff_t>(len));
        fs::path p = m_dir / "ilt.png";
        spit(p, t);
        auto b = bv->validate(t.data(), t.size());
        auto s = sv->validateFile(p);
        std::string what = std::to_string(dims.first) + "x" + std::to_string(dims.second) + " len=" + std::to_string(len);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(what + " agree", b.valid, s.valid);
        CPPUNIT_ASSERT_MESSAGE(what + " truncated interlaced PNG rejected", !b.valid);
      }
    }
  }

  /// > 8 MB noise PNG (stream path): accepted by addFile/validateOnlyFile, id == hashOnlyFile;
  /// truncated copy (still > 8 MB) rejected.
  void testLargePngAcceptedAndTruncatedRejected() {
    Bytes png = makeNoisePng(2000, 2000);
    CPPUNIT_ASSERT_MESSAGE("generated PNG is > 8 MB: " + std::to_string(png.size()), png.size() > 8 * MB);
    fs::path good = m_dir / "big.png";
    spit(good, png);

    Imager img(cfgFor(m_dir / "s1"));
    auto v = img.validateOnlyFile(good, "big.png");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("validateOnlyFile: " + v.message, ErrorCode::Ok, v.code);
    CPPUNIT_ASSERT_EQUAL(v.id, img.hashOnlyFile(good));
    auto a = img.addFile(good, "big.png");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile: " + a.message, ErrorCode::Ok, a.code);
    CPPUNIT_ASSERT_EQUAL(v.id, a.id);
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, img.addFile(good, "again.png").code);
    auto stored = img.getImageData(a.id);
    CPPUNIT_ASSERT_EQUAL(png.size(), stored.size());
    CPPUNIT_ASSERT(std::equal(png.begin(), png.end(), stored.data()));

    // The Blob API (oracle) agrees on id.
    Imager oracle(cfgFor(m_dir / "s2"));
    auto o = oracle.validateOnly(Blob::fromVector(Bytes(png)), "big.png");
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, o.code);
    CPPUNIT_ASSERT_EQUAL(o.id, v.id);

    // Truncated copies, all still > 8 MB.
    for (size_t len : {png.size() - 1, png.size() - 12, png.size() * 3 / 4, size_t(9 * MB)}) {
      fs::path bad = m_dir / "trunc.png";
      spit(bad, Bytes(png.begin(), png.begin() + static_cast<std::ptrdiff_t>(len)));
      CPPUNIT_ASSERT(fs::file_size(bad) > 8 * MB);
      auto tv = img.validateOnlyFile(bad, "trunc.png");
      CPPUNIT_ASSERT_MESSAGE("truncated PNG (" + std::to_string(len) + ") rejected by validateOnlyFile", tv.code == ErrorCode::BrokenFile);
      auto ta = img.addFile(bad, "trunc.png");
      CPPUNIT_ASSERT_MESSAGE("truncated PNG (" + std::to_string(len) + ") rejected by addFile", ta.code == ErrorCode::BrokenFile);
    }
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), img.imageCount());
  }

  /// Corrupted bytes in a > 8 MB PNG: the stream validator and the Blob validator agree.
  void testLargePngCorruptedStreamAndBlobAgree() {
    Bytes png = makeNoisePng(2000, 2000, 5);
    auto sv = createPngStreamValidator();
    auto bv = createPngValidator();
    size_t rejected = 0;
    for (size_t off : {size_t(0), size_t(12), size_t(20), size_t(40), png.size() / 4, png.size() / 2,
                       png.size() - 20, png.size() - 8, png.size() - 1}) {
      Bytes c = png;
      c[off] ^= 0xFF;
      fs::path p = m_dir / "c.png";
      spit(p, c);
      auto s = sv->validateFile(p);
      auto b = bv->validate(c.data(), c.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("flip@" + std::to_string(off) + " stream='" + s.errorMessage + "' blob='" + b.errorMessage + "'", b.valid, s.valid);
      rejected += s.valid ? 0 : 1;
    }
    CPPUNIT_ASSERT_MESSAGE("corruption detected for at least some offsets", rejected >= 3);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(StreamValidatorEquivalenceTest);
