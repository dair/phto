#include <auth/PasswordHash.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <stdexcept>
#include <string>

using auth::hashPassword;
using auth::PasswordRecord;
using auth::verifyPassword;

// ============================================================================
// PasswordHash tests
// ============================================================================
class PasswordHashTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(PasswordHashTest);
  CPPUNIT_TEST(testOutputShape);
  CPPUNIT_TEST(testCorrectPasswordVerifies);
  CPPUNIT_TEST(testWrongPasswordFails);
  CPPUNIT_TEST(testSaltRandomness);
  CPPUNIT_TEST(testTamperHashFails);
  CPPUNIT_TEST(testWrongIterationsFails);
  CPPUNIT_TEST(testUnsupportedAlgoFails);
  CPPUNIT_TEST_SUITE_END();

public:
  void testOutputShape() {
    PasswordRecord rec = hashPassword("hunter2", 1000);
    CPPUNIT_ASSERT_EQUAL(std::string("pbkdf2-sha256"), rec.algo);
    CPPUNIT_ASSERT_EQUAL(size_t(16), rec.salt.size());
    CPPUNIT_ASSERT_EQUAL(size_t(32), rec.hash.size());
    CPPUNIT_ASSERT_EQUAL(uint32_t(1000), rec.iterations);
  }

  void testCorrectPasswordVerifies() {
    PasswordRecord rec = hashPassword("correct horse battery staple", 1000);
    CPPUNIT_ASSERT(verifyPassword("correct horse battery staple", rec));
  }

  void testWrongPasswordFails() {
    PasswordRecord rec = hashPassword("correct horse battery staple", 1000);
    CPPUNIT_ASSERT(!verifyPassword("wrong password", rec));
  }

  void testSaltRandomness() {
    // Two hashes of the same password must have different salts (and different hashes).
    PasswordRecord rec1 = hashPassword("same password", 1000);
    PasswordRecord rec2 = hashPassword("same password", 1000);
    CPPUNIT_ASSERT(rec1.salt != rec2.salt);
    CPPUNIT_ASSERT(rec1.hash != rec2.hash);
    // Both must still verify correctly.
    CPPUNIT_ASSERT(verifyPassword("same password", rec1));
    CPPUNIT_ASSERT(verifyPassword("same password", rec2));
  }

  void testTamperHashFails() {
    PasswordRecord rec = hashPassword("tamper me", 1000);
    // Flip one byte in the stored hash.
    rec.hash[0] ^= 0xFF;
    CPPUNIT_ASSERT(!verifyPassword("tamper me", rec));
  }

  void testWrongIterationsFails() {
    PasswordRecord rec = hashPassword("test password", 1000);
    // Re-derive will use wrong iteration count -> mismatch.
    rec.iterations = 2000;
    CPPUNIT_ASSERT(!verifyPassword("test password", rec));
  }

  void testUnsupportedAlgoFails() {
    PasswordRecord rec = hashPassword("test password", 1000);
    rec.algo = "argon2id";
    // Must return false, not throw.
    bool result = false;
    CPPUNIT_ASSERT_NO_THROW(result = verifyPassword("test password", rec));
    CPPUNIT_ASSERT(!result);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(PasswordHashTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
