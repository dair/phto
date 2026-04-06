#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <validations/aae/aae_validator.h>

namespace {

// Minimal JPEG header bytes (SOI + APP0).
static const uint8_t JPEG_HEADER[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00};

// A minimal valid AAE plist (matches the fixture file content).
static const char VALID_AAE[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""
  " \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
  "<plist version=\"1.0\">\n"
  "<dict>\n"
  "    <key>adjustmentFormatVersion</key>\n"
  "    <integer>1</integer>\n"
  "    <key>adjustmentBaseVersion</key>\n"
  "    <integer>0</integer>\n"
  "</dict>\n"
  "</plist>\n";

// Valid XML that is NOT a plist.
static const char NON_PLIST_XML[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<root>\n"
  "    <item>value</item>\n"
  "</root>\n";

// AAE-like header but broken XML (no closing </plist>).
static const char MALFORMED_AAE[] =
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
  "<plist version=\"1.0\">\n"
  "<dict>\n"
  "    <key>broken</key>\n"
  "    <string>unclosed</string>\n";
// Intentionally missing </dict> and </plist>

} // namespace

class AaeValidatorTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AaeValidatorTest);
  CPPUNIT_TEST(testNullData);
  CPPUNIT_TEST(testEmptyData);
  CPPUNIT_TEST(testTooSmall);
  CPPUNIT_TEST(testWrongFormat_Jpeg);
  CPPUNIT_TEST(testWrongFormat_Random);
  CPPUNIT_TEST(testValidAae);
  CPPUNIT_TEST(testValidAaeFromFile);
  CPPUNIT_TEST(testMalformedXml);
  CPPUNIT_TEST(testNonPlistXml);
  CPPUNIT_TEST_SUITE_END();

public:
  void testNullData() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateAae(nullptr, 0));
  }

  void testEmptyData() {
    const uint8_t dummy = 0;
    CPPUNIT_ASSERT_EQUAL(WRONG, validateAae(&dummy, 0));
  }

  void testTooSmall() {
    const uint8_t data[32] = {};
    CPPUNIT_ASSERT_EQUAL(WRONG, validateAae(data, sizeof(data)));
  }

  void testWrongFormat_Jpeg() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateAae(JPEG_HEADER, sizeof(JPEG_HEADER)));
  }

  void testWrongFormat_Random() {
    // 1024 bytes with no XML markers
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i) {
      data[i] = static_cast<uint8_t>((i * 137 + 42) & 0xFF);
    }
    CPPUNIT_ASSERT_EQUAL(WRONG, validateAae(data.data(), data.size()));
  }

  void testValidAae() {
    CPPUNIT_ASSERT_EQUAL(VALID, validateAae(VALID_AAE, std::strlen(VALID_AAE)));
  }

  void testValidAaeFromFile() {
    std::filesystem::path fixture{AAE_FIXTURES_DIR "/valid.aae"};
    std::ifstream f(fixture, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
      // Fixture absent — skip gracefully
      return;
    }
    auto size = static_cast<std::streamsize>(f.tellg());
    f.seekg(0);
    std::vector<char> data(static_cast<size_t>(size));
    f.read(data.data(), size);
    CPPUNIT_ASSERT(f.good());
    CPPUNIT_ASSERT_EQUAL(VALID, validateAae(data.data(), data.size()));
  }

  void testMalformedXml() {
    // Has plist start but no closing </plist> — INVALID
    CPPUNIT_ASSERT_EQUAL(INVALID, validateAae(MALFORMED_AAE, std::strlen(MALFORMED_AAE)));
  }

  void testNonPlistXml() {
    // Valid XML but no <plist — WRONG
    CPPUNIT_ASSERT_EQUAL(WRONG, validateAae(NON_PLIST_XML, std::strlen(NON_PLIST_XML)));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(AaeValidatorTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
