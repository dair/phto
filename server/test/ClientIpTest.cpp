#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <server/AccessLog.h>
#include <server/ClientIp.h>

#include <stdexcept>
#include <string>
#include <vector>

using server::TrustedProxies;

class ClientIpTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ClientIpTest);
  CPPUNIT_TEST(testEmptySetIgnoresXff);
  CPPUNIT_TEST(testSingleAddressAndCidr);
  CPPUNIT_TEST(testCidrBoundaries);
  CPPUNIT_TEST(testZeroPrefixMatchesAll);
  CPPUNIT_TEST(testIpv6Cidr);
  CPPUNIT_TEST(testIpv4MappedMatchesIpv4Entry);
  CPPUNIT_TEST(testIpv4DoesNotMatchIpv6Entry);
  CPPUNIT_TEST(testInvalidEntriesThrow);
  CPPUNIT_TEST(testContainsRejectsGarbage);
  CPPUNIT_TEST(testUntrustedPeerIgnoresXff);
  CPPUNIT_TEST(testRightmostUntrustedWins);
  CPPUNIT_TEST(testSpoofedLeadingEntryIgnored);
  CPPUNIT_TEST(testTrustedHopsSkipped);
  CPPUNIT_TEST(testWhitespaceTolerated);
  CPPUNIT_TEST(testMalformedFallsBackToPeer);
  CPPUNIT_TEST(testEmptyAndAllTrustedFallBackToPeer);
  CPPUNIT_TEST(testIpv6Client);
  CPPUNIT_TEST_SUITE_END();

  static TrustedProxies mk(std::vector<std::string> v) { return TrustedProxies::parse(v); }

public:
  void testEmptySetIgnoresXff() {
    auto p = mk({});
    CPPUNIT_ASSERT(p.empty());
    CPPUNIT_ASSERT(!p.contains("127.0.0.1"));
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), p.clientIp("127.0.0.1", "1.2.3.4"));
  }
  void testSingleAddressAndCidr() {
    auto p = mk({"10.0.0.1", "192.168.0.0/16"});
    CPPUNIT_ASSERT(!p.empty());
    CPPUNIT_ASSERT(p.contains("10.0.0.1"));
    CPPUNIT_ASSERT(!p.contains("10.0.0.2"));
    CPPUNIT_ASSERT(p.contains("192.168.55.7"));
    CPPUNIT_ASSERT(!p.contains("192.169.0.1"));
  }
  void testCidrBoundaries() {
    auto p = mk({"10.0.0.0/25"});
    CPPUNIT_ASSERT(p.contains("10.0.0.0"));
    CPPUNIT_ASSERT(p.contains("10.0.0.127"));
    CPPUNIT_ASSERT(!p.contains("10.0.0.128"));
    auto q = mk({"10.0.0.5/32"});
    CPPUNIT_ASSERT(q.contains("10.0.0.5"));
    CPPUNIT_ASSERT(!q.contains("10.0.0.4"));
  }
  void testZeroPrefixMatchesAll() {
    auto p = mk({"0.0.0.0/0"});
    CPPUNIT_ASSERT(p.contains("8.8.8.8"));
    CPPUNIT_ASSERT(p.contains("255.255.255.255"));
  }
  void testIpv6Cidr() {
    auto p = mk({"::1", "fd00::/8", "2001:db8::/32"});
    CPPUNIT_ASSERT(p.contains("::1"));
    CPPUNIT_ASSERT(!p.contains("::2"));
    CPPUNIT_ASSERT(p.contains("fd12:3456::1"));
    CPPUNIT_ASSERT(!p.contains("fe80::1"));
    CPPUNIT_ASSERT(p.contains("2001:db8:ffff::9"));
    CPPUNIT_ASSERT(!p.contains("2001:db9::1"));
  }
  void testIpv4MappedMatchesIpv4Entry() {
    auto p = mk({"10.0.0.0/8"});
    CPPUNIT_ASSERT(p.contains("::ffff:10.1.2.3"));
    CPPUNIT_ASSERT(!p.contains("::ffff:11.1.2.3"));
  }
  void testIpv4DoesNotMatchIpv6Entry() {
    auto p = mk({"fd00::/8"});
    CPPUNIT_ASSERT(!p.contains("10.0.0.1"));
  }
  void testInvalidEntriesThrow() {
    for (const char* bad : {"", "not-an-ip", "10.0.0.0/33", "10.0.0.0/-1", "10.0.0.0/", "10.0.0.0/x", "::1/129",
                            "256.0.0.1", "1.2.3", "1.2.3.4.5", "10.0.0.0/8/8"}) {
      bool threw = false;
      try {
        mk({bad});
      } catch (const std::invalid_argument&) {
        threw = true;
      }
      CPPUNIT_ASSERT_MESSAGE(std::string("should reject: '") + bad + "'", threw);
    }
  }
  void testContainsRejectsGarbage() {
    auto p = mk({"0.0.0.0/0", "::/0"});
    CPPUNIT_ASSERT(!p.contains("garbage"));
    CPPUNIT_ASSERT(!p.contains(""));
  }
  void testUntrustedPeerIgnoresXff() {
    auto p = mk({"10.0.0.0/8"});
    CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.9"), p.clientIp("203.0.113.9", "10.0.0.1, 1.1.1.1"));
  }
  void testRightmostUntrustedWins() {
    auto p = mk({"127.0.0.1"});
    CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.1"), p.clientIp("127.0.0.1", "203.0.113.1"));
  }
  void testSpoofedLeadingEntryIgnored() {
    auto p = mk({"127.0.0.1"});
    CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.1"), p.clientIp("127.0.0.1", "6.6.6.6, 203.0.113.1"));
  }
  void testTrustedHopsSkipped() {
    auto p = mk({"127.0.0.1", "10.0.0.0/8"});
    CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.1"),
                         p.clientIp("127.0.0.1", "203.0.113.1, 10.1.1.1, 10.2.2.2"));
  }
  void testWhitespaceTolerated() {
    auto p = mk({"127.0.0.1"});
    CPPUNIT_ASSERT_EQUAL(std::string("203.0.113.1"), p.clientIp("127.0.0.1", "  6.6.6.6 ,\t203.0.113.1  "));
  }
  void testMalformedFallsBackToPeer() {
    auto p = mk({"127.0.0.1"});
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), p.clientIp("127.0.0.1", "not-an-ip"));
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), p.clientIp("127.0.0.1", "1.2.3.4, garbage"));
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), p.clientIp("127.0.0.1", "1.2.3.4,,"));
  }
  void testEmptyAndAllTrustedFallBackToPeer() {
    auto p = mk({"127.0.0.0/8"});
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), p.clientIp("127.0.0.1", ""));
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), p.clientIp("127.0.0.1", "127.0.0.2, 127.0.0.3"));
  }
  void testIpv6Client() {
    auto p = mk({"::1"});
    CPPUNIT_ASSERT_EQUAL(std::string("2001:db8::7"), p.clientIp("::1", "2001:db8::7"));
  }
};

class AccessLogFormatTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AccessLogFormatTest);
  CPPUNIT_TEST(testFieldLayout);
  CPPUNIT_TEST(testZeroBytesIsDash);
  CPPUNIT_TEST(testAnonymousLogin);
  CPPUNIT_TEST(testControlCharsAndSpacesEscaped);
  CPPUNIT_TEST(testBackslashEscaped);
  CPPUNIT_TEST(testHighBytesEscaped);
  CPPUNIT_TEST(testLongPathTruncated);
  CPPUNIT_TEST(testAlwaysEightFields);
  CPPUNIT_TEST_SUITE_END();

  static std::string fmt(std::string_view path, uint64_t bytes = 12, std::string_view login = "alice") {
    return server::AccessLog::format("2026-10-31T12:00:00Z", "203.0.113.5", login, "GET", path, 200, bytes, 7);
  }
  static int fields(const std::string& line) {
    int n = 1;
    for (size_t i = 0; i + 1 < line.size(); ++i) // ignore the trailing newline
      if (line[i] == ' ') ++n;
    return n;
  }

public:
  void testFieldLayout() {
    CPPUNIT_ASSERT_EQUAL(std::string("2026-10-31T12:00:00Z 203.0.113.5 alice GET /items 200 12 7ms\n"), fmt("/items"));
  }
  void testZeroBytesIsDash() {
    CPPUNIT_ASSERT_EQUAL(std::string("2026-10-31T12:00:00Z 203.0.113.5 alice GET /x 200 - 7ms\n"), fmt("/x", 0));
  }
  void testAnonymousLogin() {
    CPPUNIT_ASSERT_EQUAL(std::string("2026-10-31T12:00:00Z 203.0.113.5 - GET /health 200 12 7ms\n"), fmt("/health", 12, "-"));
  }
  void testControlCharsAndSpacesEscaped() {
    std::string line = fmt(std::string("/a\n\r\x1b[0m b"));
    CPPUNIT_ASSERT(line.find("/a\\x0a\\x0d\\x1b[0m\\x20b ") != std::string::npos);
    CPPUNIT_ASSERT_EQUAL(size_t(line.size() - 1), line.find('\n'));
    CPPUNIT_ASSERT(line.find('\r') == std::string::npos);
    CPPUNIT_ASSERT(line.find('\x1b') == std::string::npos);
  }
  void testBackslashEscaped() {
    CPPUNIT_ASSERT(fmt("/a\\b").find("/a\\x5cb ") != std::string::npos);
  }
  void testHighBytesEscaped() {
    CPPUNIT_ASSERT(fmt("/\xc3\xa9").find("/\\xc3\\xa9 ") != std::string::npos);
  }
  void testLongPathTruncated() {
    std::string line = fmt("/" + std::string(2000, 'a'));
    CPPUNIT_ASSERT(line.size() < 700);
    CPPUNIT_ASSERT(line.find("... 200 ") != std::string::npos);
  }
  void testAlwaysEightFields() {
    for (const char* p : {"/", "/a b c", "/x\ty", "/\n\n\n"})
      CPPUNIT_ASSERT_EQUAL_MESSAGE(p, 8, fields(fmt(p)));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ClientIpTest);
CPPUNIT_TEST_SUITE_REGISTRATION(AccessLogFormatTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
