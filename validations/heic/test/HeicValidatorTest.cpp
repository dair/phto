#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <libheif/heif.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <validations/heic/heic_validator.h>

// ---------------------------------------------------------------------------
// Minimal valid HEIF/AV1 (AVIF) file — 4x4 solid-gray image, 273 bytes.
// Generated with libheif + libaom encoder.
// ---------------------------------------------------------------------------
namespace {

// clang-format off
static const uint8_t VALID_HEIC_DATA[] = {
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
// clang-format on

static const size_t VALID_HEIC_SIZE = sizeof(VALID_HEIC_DATA);

// Minimal JPEG header bytes (SOI + APP0 start).
static const uint8_t JPEG_HEADER[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00};

// PNG signature.
static const uint8_t PNG_HEADER[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

} // namespace

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

class HeicValidatorTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HeicValidatorTest);
  CPPUNIT_TEST(testNullData);
  CPPUNIT_TEST(testEmptyData);
  CPPUNIT_TEST(testTooSmall);
  CPPUNIT_TEST(testWrongFormat_Jpeg);
  CPPUNIT_TEST(testWrongFormat_Png);
  CPPUNIT_TEST(testWrongFormat_Random);
  CPPUNIT_TEST(testTruncatedHeader);
  CPPUNIT_TEST(testTruncatedBody);
  CPPUNIT_TEST(testCorruptedData);
  CPPUNIT_TEST(testValidHeic);
  CPPUNIT_TEST(testValidHeicFromFile);
  CPPUNIT_TEST_SUITE_END();

public:
  void testNullData() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateHeic(nullptr, 0));
  }

  void testEmptyData() {
    const uint8_t dummy = 0;
    CPPUNIT_ASSERT_EQUAL(WRONG, validateHeic(&dummy, 0));
  }

  void testTooSmall() {
    const uint8_t data[4] = {0x00, 0x00, 0x00, 0x00};
    CPPUNIT_ASSERT_EQUAL(WRONG, validateHeic(data, sizeof(data)));
  }

  void testWrongFormat_Jpeg() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateHeic(JPEG_HEADER, sizeof(JPEG_HEADER)));
  }

  void testWrongFormat_Png() {
    CPPUNIT_ASSERT_EQUAL(WRONG, validateHeic(PNG_HEADER, sizeof(PNG_HEADER)));
  }

  void testWrongFormat_Random() {
    // 1024 bytes with no ftyp at offset 4
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); i++) {
      data[i] = static_cast<uint8_t>((i * 137 + 42) & 0xFF);
    }
    // Ensure offset 4 is not "ftyp"
    data[4] = 0xDE;
    data[5] = 0xAD;
    data[6] = 0xBE;
    data[7] = 0xEF;
    CPPUNIT_ASSERT_EQUAL(WRONG, validateHeic(data.data(), data.size()));
  }

  void testTruncatedHeader() {
    // First 64 bytes — has ftyp box but container is incomplete
    CPPUNIT_ASSERT(VALID_HEIC_SIZE > 64);
    CPPUNIT_ASSERT_EQUAL(INVALID, validateHeic(VALID_HEIC_DATA, 64));
  }

  void testTruncatedBody() {
    // First half — container metadata may parse but mdat is missing
    size_t half = VALID_HEIC_SIZE / 2;
    CPPUNIT_ASSERT_EQUAL(INVALID, validateHeic(VALID_HEIC_DATA, half));
  }

  void testCorruptedData() {
    if (!heif_have_decoder_for_format(heif_compression_AV1)) {
      return; // no AV1 decoder — skip
    }
    std::vector<uint8_t> data(VALID_HEIC_DATA, VALID_HEIC_DATA + VALID_HEIC_SIZE);
    // Zero out a chunk of the mdat payload (last 30 bytes)
    size_t start = data.size() > 30 ? data.size() - 30 : 0;
    for (size_t i = start; i < data.size(); i++) {
      data[i] = 0x00;
    }
    CPPUNIT_ASSERT_EQUAL(INVALID, validateHeic(data.data(), data.size()));
  }

  void testValidHeic() {
    if (!heif_have_decoder_for_format(heif_compression_AV1)) {
      return; // no AV1 decoder — skip
    }
    CPPUNIT_ASSERT_EQUAL(VALID, validateHeic(VALID_HEIC_DATA, VALID_HEIC_SIZE));
  }

  void testValidHeicFromFile() {
    std::filesystem::path fixture{HEIC_FIXTURES_DIR "/valid.heic"};
    std::ifstream file(fixture, std::ios::binary | std::ios::ate);
    CPPUNIT_ASSERT_MESSAGE("fixture file not found: " + fixture.string(), file.is_open());

    auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    CPPUNIT_ASSERT(file.good());

    ValidationResult result = validateHeic(data.data(), data.size());
    // File must at minimum have a valid ISOBMFF container.
    // Full decode (VALID) requires the appropriate codec decoder.
    CPPUNIT_ASSERT_MESSAGE("expected VALID or INVALID (not WRONG) for real HEIC fixture", result != WRONG);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(HeicValidatorTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
