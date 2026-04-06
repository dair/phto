#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <validations/nef/nef_validator.h>

// ---------------------------------------------------------------------------
// Minimal TIFF-based header bytes for constructing test data.
// ---------------------------------------------------------------------------
namespace {

// Minimal JPEG header bytes (SOI + APP0 start).
static const uint8_t JPEG_HEADER[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00};

// PNG signature.
static const uint8_t PNG_HEADER[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

} // namespace

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

class NefValidatorTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(NefValidatorTest);
  CPPUNIT_TEST(testNullData);
  CPPUNIT_TEST(testEmptyData);
  CPPUNIT_TEST(testTooSmall);
  CPPUNIT_TEST(testWrongFormat_Jpeg);
  CPPUNIT_TEST(testWrongFormat_Png);
  CPPUNIT_TEST(testWrongFormat_Random);
  CPPUNIT_TEST(testTruncatedHeader);
  CPPUNIT_TEST(testTruncatedBody);
  CPPUNIT_TEST(testCorruptedData);
  CPPUNIT_TEST(testValidNef);
  CPPUNIT_TEST_SUITE_END();

  std::vector<uint8_t> m_nefData;

public:
  void setUp() override {
    std::filesystem::path fixture{NEF_FIXTURES_DIR "/valid.nef"};
    std::ifstream file(fixture, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      return; // fixture absent — tests that need it will skip gracefully
    }
    auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    m_nefData.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(m_nefData.data()), size);
    if (!file.good()) {
      m_nefData.clear();
    }
  }

  void tearDown() override {
    m_nefData.clear();
  }

  void testNullData() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateNef(nullptr, 0));
  }

  void testEmptyData() {
    const uint8_t dummy = 0;
    CPPUNIT_ASSERT_EQUAL(WRONG, validateNef(&dummy, 0));
  }

  void testTooSmall() {
    const uint8_t data[4] = {0x00, 0x00, 0x00, 0x00};
    CPPUNIT_ASSERT_EQUAL(WRONG, validateNef(data, sizeof(data)));
  }

  void testWrongFormat_Jpeg() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateNef(JPEG_HEADER, sizeof(JPEG_HEADER)));
  }

  void testWrongFormat_Png() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateNef(PNG_HEADER, sizeof(PNG_HEADER)));
  }

  void testWrongFormat_Random() {
    // 1024 bytes with no TIFF header at offset 0
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); i++) {
      data[i] = static_cast<uint8_t>((i * 137 + 42) & 0xFF);
    }
    // Ensure offset 0-3 is not a valid TIFF magic
    data[0] = 0xDE;
    data[1] = 0xAD;
    data[2] = 0xBE;
    data[3] = 0xEF;
    CPPUNIT_ASSERT_EQUAL(WRONG, validateNef(data.data(), data.size()));
  }

  void testTruncatedHeader() {
    if (m_nefData.empty()) {
      return; // fixture absent — skip
    }
    // First 64 bytes — TIFF magic present but container is incomplete
    CPPUNIT_ASSERT(m_nefData.size() > 64);
    CPPUNIT_ASSERT_EQUAL(INVALID, validateNef(m_nefData.data(), 64));
  }

  void testTruncatedBody() {
    if (m_nefData.empty()) {
      return; // fixture absent — skip
    }
    // First half — metadata may parse but RAW data is missing
    size_t half = m_nefData.size() / 2;
    CPPUNIT_ASSERT_EQUAL(INVALID, validateNef(m_nefData.data(), half));
  }

  void testCorruptedData() {
    if (m_nefData.empty()) {
      return; // fixture absent — skip
    }
    std::vector<uint8_t> data(m_nefData);
    // Zero out the last quarter of the file (RAW sensor data region)
    size_t start = data.size() * 3 / 4;
    for (size_t i = start; i < data.size(); i++) {
      data[i] = 0x00;
    }
    ValidationResult result = validateNef(data.data(), data.size());
    // Corrupting the RAW data should cause either INVALID or (rarely) VALID
    // if the corruption happens to still be parseable. We accept both but not WRONG.
    CPPUNIT_ASSERT_MESSAGE("corrupted NEF should not return WRONG", result != WRONG);
  }

  void testValidNef() {
    if (m_nefData.empty()) {
      return; // fixture absent — skip
    }
    CPPUNIT_ASSERT_EQUAL(VALID, validateNef(m_nefData.data(), m_nefData.size()));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(NefValidatorTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
