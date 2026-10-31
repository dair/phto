#include <auth/Validation.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <string>

using namespace auth;

class ValidationTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ValidationTest);
  CPPUNIT_TEST(testLoginAccepts);
  CPPUNIT_TEST(testLoginLengthBounds);
  CPPUNIT_TEST(testLoginRejectsBadChars);
  CPPUNIT_TEST(testFullNameBounds);
  CPPUNIT_TEST(testPasswordBounds);
  CPPUNIT_TEST(testMessages);
  CPPUNIT_TEST_SUITE_END();

public:
  void testLoginAccepts() {
    for (const char* l : {"a", "alice", "Bob.Smith", "a_b-c@d.e", "0", "user@example.com"}) {
      CPPUNIT_ASSERT_MESSAGE(l, validateLogin(l) == nullptr);
    }
  }

  void testLoginLengthBounds() {
    CPPUNIT_ASSERT(validateLogin("") != nullptr);
    CPPUNIT_ASSERT(validateLogin(std::string(64, 'a')) == nullptr);
    CPPUNIT_ASSERT(validateLogin(std::string(65, 'a')) != nullptr);
  }

  void testLoginRejectsBadChars() {
    for (const char* l : {"a b", "a/b", "a\\b", "a:b", "é", "a%20b", "a\tb", "a+b", "a*b", "a?b"}) {
      CPPUNIT_ASSERT_MESSAGE(l, validateLogin(l) != nullptr);
    }
    CPPUNIT_ASSERT(validateLogin(std::string("a\0b", 3)) != nullptr);
  }

  void testFullNameBounds() {
    CPPUNIT_ASSERT(validateFullName("") != nullptr);
    CPPUNIT_ASSERT(validateFullName("A") == nullptr);
    CPPUNIT_ASSERT(validateFullName(std::string(255, 'x')) == nullptr);
    CPPUNIT_ASSERT(validateFullName(std::string(256, 'x')) != nullptr);
  }

  void testPasswordBounds() {
    CPPUNIT_ASSERT(validatePassword("") != nullptr);
    CPPUNIT_ASSERT(validatePassword(std::string(7, 'p')) != nullptr);
    CPPUNIT_ASSERT(validatePassword(std::string(8, 'p')) == nullptr);
    CPPUNIT_ASSERT(validatePassword(std::string(1024, 'p')) == nullptr);
    CPPUNIT_ASSERT(validatePassword(std::string(1025, 'p')) != nullptr);
  }

  void testMessages() {
    CPPUNIT_ASSERT_EQUAL(
      std::string("login must be 1..64 characters from [A-Za-z0-9._@-]"), std::string(validateLogin(""))
    );
    CPPUNIT_ASSERT_EQUAL(std::string("fullName must be 1..255 bytes"), std::string(validateFullName("")));
    CPPUNIT_ASSERT_EQUAL(std::string("password must be 8..1024 bytes"), std::string(validatePassword("x")));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ValidationTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
