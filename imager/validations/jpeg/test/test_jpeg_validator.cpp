#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestFixture.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/TestFactoryRegistry.h>

#include "jpeg_validator.h"

#include <cstring>
#include <fstream>
#include <vector>

class JpegValidatorTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(JpegValidatorTest);
    CPPUNIT_TEST(testNullPointer);
    CPPUNIT_TEST(testTooShort);
    CPPUNIT_TEST(testNonJpegData);
    CPPUNIT_TEST(testWrongMagic);
    CPPUNIT_TEST(testTruncatedAfterSOI);
    CPPUNIT_TEST(testCorruptedMiddle);
    CPPUNIT_TEST(testValidJpeg);
    CPPUNIT_TEST_SUITE_END();

public:
    // --- WRONG cases ---

    void testNullPointer() {
        CPPUNIT_ASSERT_EQUAL(WRONG, validateJpeg(nullptr, 0));
    }

    void testTooShort() {
        const unsigned char one[] = {0xFF};
        CPPUNIT_ASSERT_EQUAL(WRONG, validateJpeg(one, 1));
    }

    void testNonJpegData() {
        const char text[] = "Hello, World! This is plaintext, not a JPEG.";
        CPPUNIT_ASSERT_EQUAL(WRONG, validateJpeg(text, sizeof(text)));
    }

    void testWrongMagic() {
        // Looks like SOI first byte only — second byte is wrong.
        const unsigned char buf[] = {0xFF, 0x00, 0x00, 0x00};
        CPPUNIT_ASSERT_EQUAL(WRONG, validateJpeg(buf, sizeof(buf)));
    }

    // --- INVALID cases ---

    void testTruncatedAfterSOI() {
        // Starts with a valid SOI (FF D8) but is then immediately truncated.
        const unsigned char buf[] = {0xFF, 0xD8, 0xFF};
        CPPUNIT_ASSERT_EQUAL(INVALID, validateJpeg(buf, sizeof(buf)));
    }

    void testCorruptedMiddle() {
        // Load a real JPEG then truncate it — removes enough scan data that
        // libjpeg cannot decode all scan lines and emits a fatal warning.
        std::ifstream file(TEST_JPEG_PATH, std::ios::binary);
        CPPUNIT_ASSERT_MESSAGE("Could not open test JPEG file", file.is_open());
        std::vector<unsigned char> data(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        CPPUNIT_ASSERT(data.size() > 64);
        data.resize(data.size() / 2);  // discard back half, including EOI

        CPPUNIT_ASSERT_EQUAL(INVALID, validateJpeg(data.data(), data.size()));
    }

    // --- VALID case ---

    void testValidJpeg() {
        std::ifstream file(TEST_JPEG_PATH, std::ios::binary);
        CPPUNIT_ASSERT_MESSAGE("Could not open test JPEG file", file.is_open());
        const std::vector<unsigned char> data(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        CPPUNIT_ASSERT(!data.empty());
        CPPUNIT_ASSERT_EQUAL(VALID, validateJpeg(data.data(), data.size()));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(JpegValidatorTest);

int main() {
    CppUnit::TextUi::TestRunner runner;
    runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
    return runner.run() ? 0 : 1;
}
