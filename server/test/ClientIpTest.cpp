#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <server/AccessLog.h>
#include <server/ClientIp.h>

#include <set>
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

class AccessLogJsonTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(AccessLogJsonTest);
  CPPUNIT_TEST(testFieldOrderAndLayout);
  CPPUNIT_TEST(testAnonymousLoginIsNull);
  CPPUNIT_TEST(testZeroBytesIsNumber);
  CPPUNIT_TEST(testSingleLineWithTrailingNewline);
  CPPUNIT_TEST(testQuoteAndBackslashEscaped);
  CPPUNIT_TEST(testControlCharsEscaped);
  CPPUNIT_TEST(testNewlineCannotForgeLine);
  CPPUNIT_TEST(testInvalidUtf8Replaced);
  CPPUNIT_TEST(testValidUtf8Preserved);
  CPPUNIT_TEST(testEscapedInEveryStringField);
  CPPUNIT_TEST(testDurationIsNumber);
  CPPUNIT_TEST(testParseFormat);
  CPPUNIT_TEST(testValidRequestId);
  CPPUNIT_TEST(testGeneratedRequestIds);
  CPPUNIT_TEST_SUITE_END();

  static std::string js(
    std::string_view path = "/items",
    uint64_t bytes = 12,
    std::string_view login = "alice",
    std::string_view rid = "abc-1",
    std::string_view ip = "203.0.113.5",
    std::string_view method = "GET"
  ) {
    return server::AccessLog::formatJson("2026-10-31T12:00:00.123Z", ip, login, method, path, 200, bytes, 7000, rid);
  }
  static bool escNl(const std::string& l, const std::string& pre) {
    return l.find(pre + "\\n\"") != std::string::npos || l.find(pre + "\\u000a\"") != std::string::npos;
  }
  static bool noRawControl(const std::string& line) {
    for (size_t i = 0; i + 1 < line.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(line[i]);
      if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
  }

public:
  void testFieldOrderAndLayout() {
    std::string l = js("/items");
    size_t p = 0;
    for (const char* k : {"\"ts\":", "\"client_ip\":", "\"login\":", "\"method\":", "\"route\":", "\"path\":",
                          "\"status\":", "\"bytes\":", "\"duration_ms\":", "\"request_id\":"}) {
      size_t q = l.find(k, p);
      CPPUNIT_ASSERT_MESSAGE(k, q != std::string::npos);
      p = q;
    }
    CPPUNIT_ASSERT_EQUAL(size_t(0), l.find("{\"ts\":\"2026-10-31T12:00:00.123Z\""));
    CPPUNIT_ASSERT(l.find("\"client_ip\":\"203.0.113.5\"") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\"login\":\"alice\"") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\"method\":\"GET\"") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\"path\":\"/items\"") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\"status\":200") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\"bytes\":12") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\"request_id\":\"abc-1\"") != std::string::npos);
    CPPUNIT_ASSERT(l.find("{\"") == 0 && l.rfind("}\n") == l.size() - 2);
  }
  void testAnonymousLoginIsNull() {
    CPPUNIT_ASSERT(js("/health", 12, "").find("\"login\":null") != std::string::npos);
  }
  void testZeroBytesIsNumber() {
    CPPUNIT_ASSERT(js("/x", 0).find("\"bytes\":0,") != std::string::npos);
    CPPUNIT_ASSERT(js("/x", 0).find("\"bytes\":null") == std::string::npos);
  }
  void testSingleLineWithTrailingNewline() {
    std::string l = js("/a\n\r\x1b b");
    CPPUNIT_ASSERT_EQUAL(size_t(l.size() - 1), l.find('\n'));
    CPPUNIT_ASSERT(noRawControl(l));
  }
  void testQuoteAndBackslashEscaped() {
    std::string l = js("/a\"b\\c");
    CPPUNIT_ASSERT(l.find("\"path\":\"/a\\\"b\\\\c\"") != std::string::npos);
  }
  void testControlCharsEscaped() {
    std::string l = js(std::string("/\x01\x1f\x7f\t\b\f", 8));
    CPPUNIT_ASSERT(noRawControl(l));
    CPPUNIT_ASSERT(l.find("\\u0001") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\\u001f") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\\t") != std::string::npos || l.find("\\u0009") != std::string::npos);
  }
  void testNewlineCannotForgeLine() {
    std::string l = js("/x\n{\"ts\":\"forged\"}\n");
    size_t nl = 0;
    for (char c : l) if (c == '\n') ++nl;
    CPPUNIT_ASSERT_EQUAL(size_t(1), nl);
    CPPUNIT_ASSERT(l.find("\\n") != std::string::npos || l.find("\\u000a") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\\\"ts\\\":\\\"forged\\\"") != std::string::npos);
  }
  void testInvalidUtf8Replaced() {
    for (const char* bad : {"/\xff", "/\xc3(", "/\xed\xa0\x80", "/\xf4\x90\x80\x80", "/\xc0\xaf", "/\xe2\x82"}) {
      std::string l = js(bad);
      CPPUNIT_ASSERT_MESSAGE(bad, noRawControl(l));
      CPPUNIT_ASSERT_MESSAGE(bad, l.find('\xff') == std::string::npos);
      CPPUNIT_ASSERT_MESSAGE(bad, l.find("\xef\xbf\xbd") != std::string::npos); // U+FFFD
      CPPUNIT_ASSERT_MESSAGE(bad, l.find('\xc0') == std::string::npos);
      CPPUNIT_ASSERT_MESSAGE(bad, l.find('\xed') == std::string::npos);
    }
  }
  void testValidUtf8Preserved() {
    std::string l = js("/\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    CPPUNIT_ASSERT(l.find("\"path\":\"/\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\"") != std::string::npos
                   || l.find("\\u00e9") != std::string::npos);
    CPPUNIT_ASSERT(l.find("\xef\xbf\xbd") == std::string::npos);
  }
  void testEscapedInEveryStringField() {
    std::string l = js("/p\"", 5, "al\"ice\n", "r\"id\n", "1.2.3.4\"\n", "G\"ET\n");
    CPPUNIT_ASSERT(noRawControl(l));
    CPPUNIT_ASSERT(l.find("\"login\":\"al\\\"ice\\n\"") != std::string::npos || l.find("\"login\":\"al\\\"ice\\u000a\"") != std::string::npos);
    CPPUNIT_ASSERT(escNl(l, "\"request_id\":\"r\\\"id"));
    CPPUNIT_ASSERT(escNl(l, "\"client_ip\":\"1.2.3.4\\\""));
    CPPUNIT_ASSERT(escNl(l, "\"method\":\"G\\\"ET"));
  }
  void testDurationIsNumber() {
    std::string l = js();
    size_t p = l.find("\"duration_ms\":");
    CPPUNIT_ASSERT(p != std::string::npos);
    char c = l[p + 14];
    CPPUNIT_ASSERT(c >= '0' && c <= '9');
    CPPUNIT_ASSERT(l.find("\"duration_ms\":\"") == std::string::npos);
  }
  void testParseFormat() {
    using F = server::AccessLog::Format;
    CPPUNIT_ASSERT(server::AccessLog::parseFormat("text") == F::Text);
    CPPUNIT_ASSERT(server::AccessLog::parseFormat("json") == F::Json);
    CPPUNIT_ASSERT(!server::AccessLog::parseFormat("").has_value());
    CPPUNIT_ASSERT(!server::AccessLog::parseFormat("xml").has_value());
    CPPUNIT_ASSERT(!server::AccessLog::parseFormat("jsonl").has_value());
    CPPUNIT_ASSERT(!server::AccessLog::parseFormat("json ").has_value());
  }
  void testValidRequestId() {
    CPPUNIT_ASSERT(server::validRequestId("a"));
    CPPUNIT_ASSERT(server::validRequestId("Req-1.2_x"));
    CPPUNIT_ASSERT(server::validRequestId(std::string(64, 'a')));
    CPPUNIT_ASSERT(!server::validRequestId(""));
    CPPUNIT_ASSERT(!server::validRequestId(std::string(65, 'a')));
    for (const char* bad : {"a b", "a;b", "a\"b", "a\\b", "a/b", "a\nb", "a\tb", "a\r\nX: 1", "caf\xc3\xa9", "a%0d", "<x>", "a,b"})
      CPPUNIT_ASSERT_MESSAGE(bad, !server::validRequestId(bad));
    CPPUNIT_ASSERT(!server::validRequestId(std::string("a\0b", 3)));
  }
  void testGeneratedRequestIds() {
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
      std::string id = server::generateRequestId();
      CPPUNIT_ASSERT_MESSAGE(id, server::validRequestId(id));
      seen.insert(id);
    }
    CPPUNIT_ASSERT_EQUAL(size_t(1000), seen.size());
  }
};
CPPUNIT_TEST_SUITE_REGISTRATION(AccessLogJsonTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
