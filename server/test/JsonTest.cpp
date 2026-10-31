#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <crow.h>
#include <server/Json.h>
#include <server/LoginThrottle.h>

#include <string>

// ============================================================================
// httpStatusFor — every ErrorCode maps to the expected HTTP status
// ============================================================================
class HttpStatusForTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HttpStatusForTest);
  CPPUNIT_TEST(testOk);
  CPPUNIT_TEST(testBrokenFile);
  CPPUNIT_TEST(testDuplicateFile);
  CPPUNIT_TEST(testUnsupportedFormat);
  CPPUNIT_TEST(testFileNotFound);
  CPPUNIT_TEST(testStorageError);
  CPPUNIT_TEST(testAmbiguousSidecar);
  CPPUNIT_TEST(testDatabaseError);
  CPPUNIT_TEST(testConfigError);
  CPPUNIT_TEST(testTooLarge);
  CPPUNIT_TEST_SUITE_END();

public:
  void testOk() {
    CPPUNIT_ASSERT_EQUAL(200, server::httpStatusFor(imager::ErrorCode::Ok));
  }

  void testBrokenFile() {
    CPPUNIT_ASSERT_EQUAL(400, server::httpStatusFor(imager::ErrorCode::BrokenFile));
  }

  void testDuplicateFile() {
    CPPUNIT_ASSERT_EQUAL(409, server::httpStatusFor(imager::ErrorCode::DuplicateFile));
  }

  void testUnsupportedFormat() {
    CPPUNIT_ASSERT_EQUAL(415, server::httpStatusFor(imager::ErrorCode::UnsupportedFormat));
  }

  void testFileNotFound() {
    CPPUNIT_ASSERT_EQUAL(404, server::httpStatusFor(imager::ErrorCode::FileNotFound));
  }

  void testStorageError() {
    CPPUNIT_ASSERT_EQUAL(500, server::httpStatusFor(imager::ErrorCode::StorageError));
  }

  void testAmbiguousSidecar() {
    CPPUNIT_ASSERT_EQUAL(409, server::httpStatusFor(imager::ErrorCode::AmbiguousSidecar));
  }

  void testDatabaseError() {
    CPPUNIT_ASSERT_EQUAL(500, server::httpStatusFor(imager::ErrorCode::DatabaseError));
  }

  void testConfigError() {
    CPPUNIT_ASSERT_EQUAL(500, server::httpStatusFor(imager::ErrorCode::ConfigError));
  }

  void testTooLarge() {
    CPPUNIT_ASSERT_EQUAL(413, server::httpStatusFor(imager::ErrorCode::TooLarge));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(HttpStatusForTest);

// ============================================================================
// errorCodeName — every ErrorCode maps to its exact enum spelling
// ============================================================================
class ErrorCodeNameTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ErrorCodeNameTest);
  CPPUNIT_TEST(testOk);
  CPPUNIT_TEST(testBrokenFile);
  CPPUNIT_TEST(testDuplicateFile);
  CPPUNIT_TEST(testUnsupportedFormat);
  CPPUNIT_TEST(testFileNotFound);
  CPPUNIT_TEST(testStorageError);
  CPPUNIT_TEST(testAmbiguousSidecar);
  CPPUNIT_TEST(testDatabaseError);
  CPPUNIT_TEST(testConfigError);
  CPPUNIT_TEST(testTooLarge);
  CPPUNIT_TEST_SUITE_END();

public:
  void testOk() {
    CPPUNIT_ASSERT_EQUAL(std::string("Ok"), server::errorCodeName(imager::ErrorCode::Ok));
  }

  void testBrokenFile() {
    CPPUNIT_ASSERT_EQUAL(std::string("BrokenFile"), server::errorCodeName(imager::ErrorCode::BrokenFile));
  }

  void testDuplicateFile() {
    CPPUNIT_ASSERT_EQUAL(std::string("Conflict"), server::errorCodeName(imager::ErrorCode::DuplicateFile));
  }

  void testUnsupportedFormat() {
    CPPUNIT_ASSERT_EQUAL(std::string("UnsupportedFormat"), server::errorCodeName(imager::ErrorCode::UnsupportedFormat));
  }

  void testFileNotFound() {
    CPPUNIT_ASSERT_EQUAL(std::string("NotFound"), server::errorCodeName(imager::ErrorCode::FileNotFound));
  }

  void testStorageError() {
    CPPUNIT_ASSERT_EQUAL(std::string("Internal"), server::errorCodeName(imager::ErrorCode::StorageError));
  }

  void testAmbiguousSidecar() {
    CPPUNIT_ASSERT_EQUAL(std::string("Conflict"), server::errorCodeName(imager::ErrorCode::AmbiguousSidecar));
  }

  void testDatabaseError() {
    CPPUNIT_ASSERT_EQUAL(std::string("Internal"), server::errorCodeName(imager::ErrorCode::DatabaseError));
  }

  void testConfigError() {
    CPPUNIT_ASSERT_EQUAL(std::string("Internal"), server::errorCodeName(imager::ErrorCode::ConfigError));
  }

  void testTooLarge() {
    CPPUNIT_ASSERT_EQUAL(std::string("TooLarge"), server::errorCodeName(imager::ErrorCode::TooLarge));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ErrorCodeNameTest);

// ============================================================================
// jsonError — status, Content-Type, and JSON envelope structure
// ============================================================================
class JsonErrorTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(JsonErrorTest);
  CPPUNIT_TEST(testStatusCode);
  CPPUNIT_TEST(testContentTypeHeader);
  CPPUNIT_TEST(testEnvelopeCode);
  CPPUNIT_TEST(testEnvelopeMessage);
  CPPUNIT_TEST(testMessageWithSpecialChars);
  CPPUNIT_TEST_SUITE_END();

public:
  void testStatusCode() {
    crow::response res = server::jsonError(404, "NotFound", "not here");
    CPPUNIT_ASSERT_EQUAL(404, res.code);
  }

  void testContentTypeHeader() {
    crow::response res = server::jsonError(200, "Ok", "good");
    CPPUNIT_ASSERT_EQUAL(std::string("application/json"), res.get_header_value("Content-Type"));
  }

  void testEnvelopeCode() {
    crow::response res = server::jsonError(409, "DuplicateFile", "already exists");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("DuplicateFile"), std::string(parsed["error"]["code"].s()));
  }

  void testEnvelopeMessage() {
    crow::response res = server::jsonError(422, "BrokenFile", "validation failed");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("validation failed"), std::string(parsed["error"]["message"].s()));
  }

  void testMessageWithSpecialChars() {
    // A message containing a double quote and backslash must round-trip correctly.
    std::string msg = R"(file "foo\bar" rejected)";
    crow::response res = server::jsonError(422, "BrokenFile", msg);
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(msg, std::string(parsed["error"]["message"].s()));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(JsonErrorTest);

// ============================================================================
// errorResponse — status and envelope code derived from ErrorCode table
// ============================================================================
class ErrorResponseTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ErrorResponseTest);
  CPPUNIT_TEST(testBrokenFileStatus);
  CPPUNIT_TEST(testBrokenFileEnvelopeCode);
  CPPUNIT_TEST(testFileNotFoundStatus);
  CPPUNIT_TEST(testFileNotFoundEnvelopeCode);
  CPPUNIT_TEST(testDatabaseErrorStatus);
  CPPUNIT_TEST(testDatabaseErrorEnvelopeCode);
  CPPUNIT_TEST(testMessagePassthrough);
  CPPUNIT_TEST_SUITE_END();

public:
  void testBrokenFileStatus() {
    crow::response res = server::errorResponse(imager::ErrorCode::BrokenFile, "bad file");
    CPPUNIT_ASSERT_EQUAL(400, res.code);
  }

  void testBrokenFileEnvelopeCode() {
    crow::response res = server::errorResponse(imager::ErrorCode::BrokenFile, "bad file");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("BrokenFile"), std::string(parsed["error"]["code"].s()));
  }

  void testFileNotFoundStatus() {
    crow::response res = server::errorResponse(imager::ErrorCode::FileNotFound, "no such id");
    CPPUNIT_ASSERT_EQUAL(404, res.code);
  }

  void testFileNotFoundEnvelopeCode() {
    crow::response res = server::errorResponse(imager::ErrorCode::FileNotFound, "no such id");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("NotFound"), std::string(parsed["error"]["code"].s()));
  }

  void testDatabaseErrorStatus() {
    crow::response res = server::errorResponse(imager::ErrorCode::DatabaseError, "db fail");
    CPPUNIT_ASSERT_EQUAL(500, res.code);
  }

  void testDatabaseErrorEnvelopeCode() {
    crow::response res = server::errorResponse(imager::ErrorCode::DatabaseError, "db fail");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("Internal"), std::string(parsed["error"]["code"].s()));
  }

  void testMessagePassthrough() {
    std::string msg = "storage I/O error on /mnt/disk2";
    crow::response res = server::errorResponse(imager::ErrorCode::StorageError, msg);
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(msg, std::string(parsed["error"]["message"].s()));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ErrorResponseTest);

// ============================================================================
// Transport-level helpers: badRequest / unauthorized / forbidden
// ============================================================================
class TransportHelpersTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TransportHelpersTest);
  CPPUNIT_TEST(testBadRequestStatus);
  CPPUNIT_TEST(testBadRequestEnvelopeCode);
  CPPUNIT_TEST(testUnauthorizedStatus);
  CPPUNIT_TEST(testUnauthorizedEnvelopeCode);
  CPPUNIT_TEST(testUnauthorizedWWWAuthenticateHeader);
  CPPUNIT_TEST(testForbiddenStatus);
  CPPUNIT_TEST(testForbiddenEnvelopeCode);
  CPPUNIT_TEST_SUITE_END();

public:
  void testBadRequestStatus() {
    crow::response res = server::badRequest("missing field");
    CPPUNIT_ASSERT_EQUAL(400, res.code);
  }

  void testBadRequestEnvelopeCode() {
    crow::response res = server::badRequest("missing field");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("BadRequest"), std::string(parsed["error"]["code"].s()));
  }

  void testUnauthorizedStatus() {
    crow::response res = server::unauthorized("token expired");
    CPPUNIT_ASSERT_EQUAL(401, res.code);
  }

  void testUnauthorizedEnvelopeCode() {
    crow::response res = server::unauthorized("token expired");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("Unauthorized"), std::string(parsed["error"]["code"].s()));
  }

  void testUnauthorizedWWWAuthenticateHeader() {
    crow::response res = server::unauthorized("token expired");
    CPPUNIT_ASSERT_EQUAL(std::string("Bearer"), res.get_header_value("WWW-Authenticate"));
  }

  void testForbiddenStatus() {
    crow::response res = server::forbidden("read-only account");
    CPPUNIT_ASSERT_EQUAL(403, res.code);
  }

  void testForbiddenEnvelopeCode() {
    crow::response res = server::forbidden("read-only account");
    crow::json::rvalue parsed = crow::json::load(res.body);
    CPPUNIT_ASSERT(parsed);
    CPPUNIT_ASSERT_EQUAL(std::string("Forbidden"), std::string(parsed["error"]["code"].s()));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TransportHelpersTest);

// ============================================================================
// validTagName / isValidUtf8 — J1 input validation
// ============================================================================
class TagNameTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TagNameTest);
  CPPUNIT_TEST(testAccepts);
  CPPUNIT_TEST(testRejectsEmptyAndTooLong);
  CPPUNIT_TEST(testRejectsCommaAndControl);
  CPPUNIT_TEST(testRejectsNulAndDel);
  CPPUNIT_TEST(testUtf8);
  CPPUNIT_TEST(testHelpers);
  CPPUNIT_TEST_SUITE_END();

public:
  void testAccepts() {
    CPPUNIT_ASSERT(server::validTagName("holiday 2024"));
    CPPUNIT_ASSERT(server::validTagName("\xD0\xBE\xD1\x82\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA")); // Cyrillic
    CPPUNIT_ASSERT(server::validTagName(std::string(255, 'a')));
  }

  void testRejectsEmptyAndTooLong() {
    CPPUNIT_ASSERT(!server::validTagName(""));
    CPPUNIT_ASSERT(!server::validTagName(std::string(256, 'a')));
  }

  void testRejectsCommaAndControl() {
    CPPUNIT_ASSERT(!server::validTagName("a,b"));
    CPPUNIT_ASSERT(!server::validTagName("a\tb"));
    CPPUNIT_ASSERT(!server::validTagName("a\nb"));
    CPPUNIT_ASSERT(!server::validTagName("\x1F"));
  }

  void testRejectsNulAndDel() {
    CPPUNIT_ASSERT(!server::validTagName(std::string("a\0b", 3)));
    CPPUNIT_ASSERT(!server::validTagName("a\x7F"));
  }

  void testUtf8() {
    CPPUNIT_ASSERT(!server::validTagName("\xFF"));
    CPPUNIT_ASSERT(!server::validTagName("\xC0\xAF"));         // overlong '/'
    CPPUNIT_ASSERT(!server::validTagName("\xED\xA0\x80"));     // surrogate
    CPPUNIT_ASSERT(!server::validTagName("\xF4\x90\x80\x80")); // > U+10FFFF
    CPPUNIT_ASSERT(!server::validTagName("\xE2\x82"));         // truncated
    CPPUNIT_ASSERT(server::validTagName("\xF0\x9F\x98\x80"));  // U+1F600
  }

  void testHelpers() {
    crow::response r = server::notFound("x");
    CPPUNIT_ASSERT_EQUAL(404, r.code);
    CPPUNIT_ASSERT_EQUAL(std::string("NotFound"), std::string(crow::json::load(r.body)["error"]["code"].s()));
    r = server::conflict("x");
    CPPUNIT_ASSERT_EQUAL(409, r.code);
    CPPUNIT_ASSERT_EQUAL(std::string("Conflict"), std::string(crow::json::load(r.body)["error"]["code"].s()));
    r = server::tooLarge("x");
    CPPUNIT_ASSERT_EQUAL(413, r.code);
    r = server::internalError();
    CPPUNIT_ASSERT_EQUAL(500, r.code);
    CPPUNIT_ASSERT_EQUAL(std::string("Internal"), std::string(crow::json::load(r.body)["error"]["code"].s()));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TagNameTest);

// ============================================================================
// LoginThrottle — per-key threshold, independence of keys, success reset
// ============================================================================
class LoginThrottleTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(LoginThrottleTest);
  CPPUNIT_TEST(testLocksAtThreshold);
  CPPUNIT_TEST(testHigherThresholdForIp);
  CPPUNIT_TEST(testSuccessResets);
  CPPUNIT_TEST_SUITE_END();

public:
  void testLocksAtThreshold() {
    server::LoginThrottle t{3};
    for (int i = 0; i < 2; ++i) {
      t.recordFailure("a");
    }
    CPPUNIT_ASSERT(!t.isLockedOut("a"));
    t.recordFailure("a");
    CPPUNIT_ASSERT(t.isLockedOut("a"));
    CPPUNIT_ASSERT(!t.isLockedOut("b"));
  }

  // Rotating logins from one IP: no single login locks, the IP counter does.
  void testHigherThresholdForIp() {
    server::LoginThrottle perLogin{server::LoginThrottle::MAX_FAILURES};
    server::LoginThrottle perIp{server::LoginThrottle::MAX_IP_FAILURES};
    for (int i = 0; i < server::LoginThrottle::MAX_IP_FAILURES; ++i) {
      CPPUNIT_ASSERT(!perIp.isLockedOut("10.0.0.1"));
      perLogin.recordFailure("user" + std::to_string(i));
      perIp.recordFailure("10.0.0.1");
    }
    CPPUNIT_ASSERT(!perLogin.isLockedOut("user0"));
    CPPUNIT_ASSERT(perIp.isLockedOut("10.0.0.1"));
    CPPUNIT_ASSERT(!perIp.isLockedOut("10.0.0.2"));
  }

  void testSuccessResets() {
    server::LoginThrottle t{3};
    t.recordFailure("a");
    t.recordFailure("a");
    t.recordSuccess("a");
    t.recordFailure("a");
    CPPUNIT_ASSERT(!t.isLockedOut("a"));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(LoginThrottleTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
