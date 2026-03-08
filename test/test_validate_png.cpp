#include <cppunit/TestCase.h>
#include <cppunit/TestFixture.h>
#include <cppunit/TestSuite.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <cstdint>
#include <cstring>
#include <setjmp.h>
#include <vector>

#include <png.h>

#include "validate_png.h"

// ---------------------------------------------------------------------------
// Helper: write a minimal 1x1 grayscale PNG into a memory buffer
// ---------------------------------------------------------------------------
namespace {

struct WriteState {
    std::vector<uint8_t>* buf;
};

void pngWriteCallback(png_structp png_ptr, png_bytep data, png_size_t len)
{
    auto* s = static_cast<WriteState*>(png_get_io_ptr(png_ptr));
    s->buf->insert(s->buf->end(), data, data + len);
}

void pngFlushCallback(png_structp) {}

std::vector<uint8_t> makeValidPng()
{
    std::vector<uint8_t> buf;
    WriteState state{&buf};

    png_structp png_ptr  = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop   info_ptr = png_create_info_struct(png_ptr);

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return {};
    }

    png_set_write_fn(png_ptr, &state, pngWriteCallback, pngFlushCallback);
    png_set_IHDR(png_ptr, info_ptr,
                 1, 1, 8,
                 PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    uint8_t row[1] = {0xFF};
    png_write_row(png_ptr, row);
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------
class ValidatePngTest : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(ValidatePngTest);
    CPPUNIT_TEST(testValidPng);
    CPPUNIT_TEST(testWrongNotPng);
    CPPUNIT_TEST(testWrongEmpty);
    CPPUNIT_TEST(testWrongTooShort);
    CPPUNIT_TEST(testInvalidCorruptAfterSignature);
    CPPUNIT_TEST(testInvalidTruncated);
    CPPUNIT_TEST_SUITE_END();

public:
    void testValidPng()
    {
        std::vector<uint8_t> png = makeValidPng();
        CPPUNIT_ASSERT(!png.empty());
        CPPUNIT_ASSERT_EQUAL(VALID, validatePng(png.data(), png.size()));
    }

    void testWrongNotPng()
    {
        const char data[] = "This is not a PNG file at all.";
        CPPUNIT_ASSERT_EQUAL(WRONG, validatePng(data, sizeof(data) - 1));
    }

    void testWrongEmpty()
    {
        CPPUNIT_ASSERT_EQUAL(WRONG, validatePng(nullptr, 0));
    }

    void testWrongTooShort()
    {
        // Fewer than 8 bytes — cannot even hold the signature
        const uint8_t data[] = {0x89, 0x50};
        CPPUNIT_ASSERT_EQUAL(WRONG, validatePng(data, sizeof(data)));
    }

    void testInvalidCorruptAfterSignature()
    {
        // Correct PNG signature followed by garbage
        uint8_t data[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, // signature
            0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF    // garbage
        };
        CPPUNIT_ASSERT_EQUAL(INVALID, validatePng(data, sizeof(data)));
    }

    void testInvalidTruncated()
    {
        // A real PNG, but truncated partway through
        std::vector<uint8_t> png = makeValidPng();
        CPPUNIT_ASSERT(!png.empty());
        // Keep only the signature + a few bytes — not a complete PNG
        std::vector<uint8_t> truncated(png.begin(), png.begin() + 12);
        CPPUNIT_ASSERT_EQUAL(INVALID, validatePng(truncated.data(), truncated.size()));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ValidatePngTest);

int main()
{
    CppUnit::TextUi::TestRunner runner;
    CppUnit::TestFactoryRegistry& registry = CppUnit::TestFactoryRegistry::getRegistry();
    runner.addTest(registry.makeTest());
    return runner.run() ? 0 : 1;
}
