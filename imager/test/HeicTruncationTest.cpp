// HeicTruncationTest.cpp — fix step fx2 / F1
//
// Truncated HEIC files must be rejected by BOTH HEIC validators (Blob and stream), and the two
// must agree. Valid HEICs generated in-test with libheif's HEVC encoder (several sizes, with and
// without alpha, grid/tiled) must still be accepted by both validators and by addFile
// (guards against over-tightening).

#include <config/Config.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <imager/Imager.h>
#include <imager/Validators.h>
#include <libheif/heif.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace imager;

namespace {

using Bytes = std::vector<uint8_t>;

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

heif_error writeCb(heif_context*, const void* data, size_t size, void* userdata) {
  auto* v = static_cast<Bytes*>(userdata);
  const auto* p = static_cast<const uint8_t*>(data);
  v->insert(v->end(), p, p + size);
  return {heif_error_Ok, heif_suberror_Unspecified, "ok"};
}

void check(const heif_error& e, const char* what) {
  if (e.code != heif_error_Ok) {
    CPPUNIT_FAIL(std::string("libheif ") + what + " failed: " + (e.message ? e.message : "?"));
  }
}

heif_writer kWriter{1, &writeCb};

/// Gradient/noise RGB(A) image of w x h.
heif_image* makeImage(int w, int h, bool alpha, uint32_t seed) {
  heif_image* img = nullptr;
  check(heif_image_create(w, h, heif_colorspace_RGB, alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB, &img),
        "image_create");
  check(heif_image_add_plane(img, heif_channel_interleaved, w, h, alpha ? 32 : 24), "add_plane");
  int stride = 0;
  uint8_t* d = heif_image_get_plane(img, heif_channel_interleaved, &stride);
  CPPUNIT_ASSERT(d != nullptr);
  const int bpp = alpha ? 4 : 3;
  uint32_t s = seed;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      s = s * 1664525u + 1013904223u;
      uint8_t* px = d + static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x) * static_cast<size_t>(bpp);
      px[0] = static_cast<uint8_t>(x * 255 / std::max(1, w - 1));
      px[1] = static_cast<uint8_t>(y * 255 / std::max(1, h - 1));
      px[2] = static_cast<uint8_t>(s >> 24);
      if (alpha) {
        px[3] = static_cast<uint8_t>((x + y) * 255 / std::max(1, w + h - 2));
      }
    }
  }
  return img;
}

/// Encode a single-image HEIC, or a rows x cols grid of (w x h) tiles when rows*cols > 1.
Bytes makeHeic(int w, int h, bool alpha, int rows = 1, int cols = 1, bool lossless = false) {
  heif_context* ctx = heif_context_alloc();
  heif_encoder* enc = nullptr;
  check(heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc), "get_encoder(HEVC)");
  heif_encoder_set_lossy_quality(enc, 60);
  if (lossless) {
    heif_encoder_set_lossless(enc, 1);
  }
  heif_image_handle* handle = nullptr;
  if (rows * cols == 1) {
    heif_image* img = makeImage(w, h, alpha, 1);
    check(heif_context_encode_image(ctx, img, enc, nullptr, &handle), "encode_image");
    heif_image_release(img);
  } else {
    std::vector<heif_image*> tiles;
    for (int i = 0; i < rows * cols; ++i) {
      tiles.push_back(makeImage(w, h, alpha, static_cast<uint32_t>(i + 1)));
    }
    check(heif_context_encode_grid(ctx, tiles.data(), static_cast<uint16_t>(rows), static_cast<uint16_t>(cols), enc, nullptr, &handle),
          "encode_grid");
    for (auto* t : tiles) {
      heif_image_release(t);
    }
  }
  heif_image_handle_release(handle);
  heif_encoder_release(enc);
  Bytes out;
  check(heif_context_write(ctx, &kWriter, &out), "context_write");
  heif_context_free(ctx);
  CPPUNIT_ASSERT_MESSAGE("generated HEIC is non-empty", !out.empty());
  return out;
}

struct Gen {
  std::string name;
  Bytes data;
};

std::vector<Gen> generated() {
  std::vector<Gen> v;
  v.push_back({"16x16", makeHeic(16, 16, false)});
  v.push_back({"17x13-odd", makeHeic(17, 13, false)});
  v.push_back({"64x48", makeHeic(64, 48, false)});
  v.push_back({"256x192", makeHeic(256, 192, false)});
  v.push_back({"640x480", makeHeic(640, 480, false)});
  v.push_back({"64x48-alpha", makeHeic(64, 48, true)});
  v.push_back({"320x240-alpha", makeHeic(320, 240, true)});
  v.push_back({"128x96-lossless", makeHeic(128, 96, false, 1, 1, true)});
  v.push_back({"grid-2x2-of-64x64", makeHeic(64, 64, false, 2, 2)});
  v.push_back({"grid-1x3-of-64x64", makeHeic(64, 64, false, 1, 3)});
  return v;
}

} // namespace

class HeicTruncationTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HeicTruncationTest);
  CPPUNIT_TEST(testFixtureTruncatedAtEveryPercentRejectedByBoth);
  CPPUNIT_TEST(testFixtureTailCutsRejectedByBoth);
  CPPUNIT_TEST(testFixtureStillAccepted);
  CPPUNIT_TEST(testGeneratedHeicsAcceptedByBothValidators);
  CPPUNIT_TEST(testGeneratedHeicsAcceptedByAddFile);
  CPPUNIT_TEST(testGeneratedHeicsTruncatedRejectedByBoth);
  CPPUNIT_TEST(testTruncatedHeicRejectedByAddFileAndValidateOnlyFile);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_dir;

  static Bytes prefix(const Bytes& b, size_t len) {
    return Bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(len));
  }

  /// Both validators reject `bytes` and agree.
  void expectRejected(const Bytes& bytes, const std::string& what) {
    fs::path p = m_dir / "t.heic";
    spit(p, bytes);
    auto b = createHeicValidator()->validate(bytes.data(), bytes.size());
    auto s = createHeicStreamValidator()->validateFile(p);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(what + ": blob/stream agree (blob='" + b.errorMessage + "' stream='" + s.errorMessage + "')", b.valid, s.valid);
    CPPUNIT_ASSERT_MESSAGE(what + ": blob validator rejects truncated HEIC", !b.valid);
    CPPUNIT_ASSERT_MESSAGE(what + ": stream validator rejects truncated HEIC", !s.valid);
    CPPUNIT_ASSERT_MESSAGE(what + ": rejection carries a message", !b.errorMessage.empty() && !s.errorMessage.empty());
  }

  void expectAccepted(const Bytes& bytes, const std::string& what) {
    fs::path p = m_dir / "ok.heic";
    spit(p, bytes);
    auto b = createHeicValidator()->validate(bytes.data(), bytes.size());
    auto s = createHeicStreamValidator()->validateFile(p);
    CPPUNIT_ASSERT_MESSAGE(what + ": blob validator accepts: " + b.errorMessage, b.valid);
    CPPUNIT_ASSERT_MESSAGE(what + ": stream validator accepts: " + s.errorMessage, s.valid);
  }

  static config::AppConfig cfgFor(const fs::path& base) {
    fs::create_directories(base / "storage");
    config::AppConfig cfg;
    cfg.targets.push_back({base / "storage", base / "imager.db"});
    return cfg;
  }

public:
  void setUp() override {
    static std::atomic<int> n{0};
    m_dir = fs::temp_directory_path() / ("imager_heictr_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
    fs::create_directories(m_dir);
  }
  void tearDown() override {
    std::error_code ec;
    fs::remove_all(m_dir, ec);
  }

  void testFixtureTruncatedAtEveryPercentRejectedByBoth() {
    Bytes b = slurp(HEIC_FIXTURES_DIR "/valid.heic");
    CPPUNIT_ASSERT(b.size() > 1024);
    for (int pct = 1; pct <= 99; ++pct) {
      expectRejected(prefix(b, b.size() * static_cast<size_t>(pct) / 100), "valid.heic @" + std::to_string(pct) + "%");
    }
  }

  void testFixtureTailCutsRejectedByBoth() {
    Bytes b = slurp(HEIC_FIXTURES_DIR "/valid.heic");
    for (size_t cut : {size_t(1), size_t(2), size_t(16), size_t(64), size_t(256), size_t(1024)}) {
      expectRejected(prefix(b, b.size() - cut), "valid.heic minus last " + std::to_string(cut) + " bytes");
    }
    for (size_t len : {size_t(0), size_t(1), size_t(8), size_t(12), size_t(16), size_t(32), size_t(64)}) {
      expectRejected(prefix(b, len), "valid.heic head " + std::to_string(len) + " bytes");
    }
  }

  void testFixtureStillAccepted() {
    expectAccepted(slurp(HEIC_FIXTURES_DIR "/valid.heic"), "valid.heic");
  }

  void testGeneratedHeicsAcceptedByBothValidators() {
    for (auto& g : generated()) {
      expectAccepted(g.data, "generated " + g.name);
    }
  }

  void testGeneratedHeicsAcceptedByAddFile() {
    Imager img(cfgFor(m_dir / "add"));
    int i = 0;
    for (auto& g : generated()) {
      fs::path p = m_dir / ("g" + std::to_string(i++) + ".heic");
      spit(p, g.data);
      auto v = img.validateOnlyFile(p, p.filename().string());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("validateOnlyFile " + g.name + ": " + v.message, ErrorCode::Ok, v.code);
      auto a = img.addFile(p, p.filename().string());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile " + g.name + ": " + a.message, ErrorCode::Ok, a.code);
      CPPUNIT_ASSERT_EQUAL(v.id, a.id);
    }
  }

  /// Every generated HEIC cut at ~10 points (incl. 1/16/1024-byte tails when the file is that big).
  void testGeneratedHeicsTruncatedRejectedByBoth() {
    for (auto& g : generated()) {
      const size_t n = g.data.size();
      std::vector<size_t> lens;
      for (int pct : {1, 10, 25, 50, 75, 90, 95, 99}) {
        lens.push_back(n * static_cast<size_t>(pct) / 100);
      }
      for (size_t cut : {size_t(1), size_t(16), size_t(1024)}) {
        if (n > cut) {
          lens.push_back(n - cut);
        }
      }
      for (size_t len : lens) {
        if (len >= n) {
          continue;
        }
        expectRejected(prefix(g.data, len), "generated " + g.name + " len=" + std::to_string(len) + "/" + std::to_string(n));
      }
    }
  }

  void testTruncatedHeicRejectedByAddFileAndValidateOnlyFile() {
    Bytes b = slurp(HEIC_FIXTURES_DIR "/valid.heic");
    Imager img(cfgFor(m_dir / "rej"));
    for (size_t cut : {size_t(1), size_t(1024), b.size() / 2}) {
      fs::path p = m_dir / "trunc.heic";
      spit(p, prefix(b, b.size() - cut));
      auto v = img.validateOnlyFile(p, "trunc.heic");
      CPPUNIT_ASSERT_EQUAL_MESSAGE("validateOnlyFile cut=" + std::to_string(cut), ErrorCode::BrokenFile, v.code);
      auto a = img.addFile(p, "trunc.heic");
      CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile cut=" + std::to_string(cut), ErrorCode::BrokenFile, a.code);
      auto ba = img.addImage(Blob::fromVector(prefix(b, b.size() - cut)), "trunc.heic");
      CPPUNIT_ASSERT_EQUAL_MESSAGE("addImage cut=" + std::to_string(cut), ErrorCode::BrokenFile, ba.code);
    }
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), img.imageCount());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(HeicTruncationTest);
