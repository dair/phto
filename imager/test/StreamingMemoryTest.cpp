// StreamingMemoryTest.cpp
//
// Ingest memory must be bounded for every file size: a mid-size (200 MB), valid,
// accepted MOV — below the historical 256 MB streaming threshold — must not be
// read whole into memory by addFile / validateOnlyFile / hashOnlyFile / deleteFile.
//
// Runs as its own process (imager_memory_tests) so VmHWM (peak RSS, from
// /proc/self/status) is not polluted by other suites. The file is written with
// plain buffered writes of a small chunk; page cache is not counted in RSS.

#include <config/Config.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <imager/Imager.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace imager;

namespace {

constexpr uint64_t MB = 1024ULL * 1024ULL;
constexpr uint64_t FILE_MB = 200;
constexpr uint64_t SMALL_FILE_MB = 64; // fully in memory under the old 256 MB threshold
/// Allowed growth of peak RSS over the pre-test baseline. The file is 200 MB;
/// bounded streaming should stay far below it (generous: covers decoder buffers).
constexpr uint64_t HWM_GROWTH_LIMIT_MB = 48; // threshold 8 MB + 4 MB chunk + decoder/allocator slack

uint64_t readStatusKb(const char* key) {
  std::ifstream in("/proc/self/status");
  std::string line;
  const std::string k = std::string(key) + ":";
  while (std::getline(in, line)) {
    if (line.rfind(k, 0) == 0) {
      return std::stoull(line.substr(k.size()));
    }
  }
  return 0;
}

} // namespace

class StreamingMemoryTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(StreamingMemoryTest);
  CPPUNIT_TEST(test64MbMovIngestIsMemoryBounded);
  CPPUNIT_TEST(testMidSizeMovIngestIsMemoryBounded);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_base;

public:
  void setUp() override {
    m_base = fs::temp_directory_path() / ("imager_mem_" + std::to_string(::getpid()));
    fs::create_directories(m_base / "storage");
  }
  void tearDown() override {
    std::error_code ec;
    fs::remove_all(m_base, ec); // big temp file removed on every exit path
  }

  void test64MbMovIngestIsMemoryBounded() {
    runIngest(SMALL_FILE_MB);
  }

  void testMidSizeMovIngestIsMemoryBounded() {
    runIngest(FILE_MB);
  }

  void runIngest(uint64_t fileMb) {
    std::ifstream in(fs::path(MOV_FIXTURES_DIR "/valid.mov"), std::ios::binary);
    CPPUNIT_ASSERT_MESSAGE("MOV fixture readable", in.good());
    std::vector<char> mov((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CPPUNIT_ASSERT(!mov.empty());

    const uint64_t size = fileMb * MB;
    fs::path big = m_base / "mid.mov";
    {
      std::ofstream out(big, std::ios::binary | std::ios::trunc);
      out.write(mov.data(), static_cast<std::streamsize>(mov.size()));
      std::vector<char> zeros(1 * MB, 0);
      uint64_t written = mov.size();
      while (written < size) {
        auto n = static_cast<size_t>(std::min<uint64_t>(zeros.size(), size - written));
        out.write(zeros.data(), static_cast<std::streamsize>(n));
        written += n;
      }
      CPPUNIT_ASSERT(out.good());
    }
    CPPUNIT_ASSERT_EQUAL(size, static_cast<uint64_t>(fs::file_size(big)));
    mov.clear();
    mov.shrink_to_fit();

    config::AppConfig cfg;
    cfg.targets.push_back({m_base / "storage", m_base / "imager.db"});
    cfg.fileSizeLimits["mov"] = 100ULL * 1024ULL * MB;

    const uint64_t baseKb = readStatusKb("VmHWM");
    auto growthMb = [&]() { return (readStatusKb("VmHWM") - baseKb) / 1024; };
    std::cerr << "\n[mem] baseline VmHWM=" << baseKb / 1024 << " MB, file=" << fileMb << " MB\n";

    Imager img(cfg);

    auto v = img.validateOnlyFile(big, "mid.mov");
    std::cerr << "[mem] after validateOnlyFile: +" << growthMb() << " MB\n";
    CPPUNIT_ASSERT_EQUAL_MESSAGE("validateOnlyFile accepts: " + v.message, ErrorCode::Ok, v.code);
    CPPUNIT_ASSERT_MESSAGE("validateOnlyFile peak RSS bounded", growthMb() < HWM_GROWTH_LIMIT_MB);

    auto h = img.hashOnlyFile(big);
    std::cerr << "[mem] after hashOnlyFile: +" << growthMb() << " MB\n";
    CPPUNIT_ASSERT_EQUAL(v.id, h);
    CPPUNIT_ASSERT_MESSAGE("hashOnlyFile peak RSS bounded", growthMb() < HWM_GROWTH_LIMIT_MB);

    auto a = img.addFile(big, "mid.mov");
    std::cerr << "[mem] after addFile: +" << growthMb() << " MB\n";
    CPPUNIT_ASSERT_EQUAL_MESSAGE("addFile accepts: " + a.message, ErrorCode::Ok, a.code);
    CPPUNIT_ASSERT_EQUAL(v.id, a.id);
    CPPUNIT_ASSERT_MESSAGE("addFile peak RSS bounded", growthMb() < HWM_GROWTH_LIMIT_MB);

    auto dup = img.addFile(big, "mid_copy.mov");
    std::cerr << "[mem] after duplicate addFile: +" << growthMb() << " MB\n";
    CPPUNIT_ASSERT_EQUAL(ErrorCode::DuplicateFile, dup.code);
    CPPUNIT_ASSERT_MESSAGE("duplicate addFile peak RSS bounded", growthMb() < HWM_GROWTH_LIMIT_MB);

    auto d = img.deleteFile(big);
    std::cerr << "[mem] after deleteFile: +" << growthMb() << " MB\n";
    CPPUNIT_ASSERT_EQUAL(ErrorCode::Ok, d.code);
    CPPUNIT_ASSERT_EQUAL(v.id, d.id);
    CPPUNIT_ASSERT_MESSAGE("deleteFile peak RSS bounded", growthMb() < HWM_GROWTH_LIMIT_MB);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(StreamingMemoryTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
