#include <auth/TokenService.h>
#include <auth/types/User.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <chrono>
#include <jwt/jwt.hpp>
#include <string>

using auth::TokenService;
using auth::User;

namespace {

User makeUser(const std::string& login, const std::string& fullName, bool isAdmin = false) {
  User u;
  u.login = login;
  u.fullName = fullName;
  u.isAdmin = isAdmin;
  u.enabled = true;
  return u;
}

/// Mint a token directly via cpp-jwt bypassing TokenService (for adversarial tests).
std::string mintToken(
  const std::string& secret,
  const std::string& issuer,
  std::chrono::system_clock::time_point exp,
  const std::string& sub,
  const std::string& name,
  const std::string& role
) {
  using namespace jwt::params;
  jwt::jwt_object obj{algorithm("HS256"), jwt::params::secret(secret), payload({{"iss", issuer}})};
  obj.add_claim("sub", sub)
    .add_claim("name", name)
    .add_claim("role", role)
    .add_claim("iat", std::chrono::system_clock::now())
    .add_claim("exp", exp)
    .add_claim("jti", std::string{"test-jti"});
  return obj.signature();
}

} // namespace

class TokenServiceTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TokenServiceTest);
  CPPUNIT_TEST(testRoundTripRegularUser);
  CPPUNIT_TEST(testRoundTripAdminUser);
  CPPUNIT_TEST(testExpiredTokenRejected);
  CPPUNIT_TEST(testWrongSecretRejected);
  CPPUNIT_TEST(testTamperedTokenRejected);
  CPPUNIT_TEST(testWrongIssuerRejected);
  CPPUNIT_TEST(testGarbageInputRejected);
  CPPUNIT_TEST(testJtiUniquenessAcrossIssues);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() override {
    m_secret = "test-secret-at-least-32-bytes-long!!";
    m_issuer = "phto-test";
    m_ttl = 3600;
    m_svc = std::make_unique<TokenService>(m_secret, m_issuer, m_ttl);
  }

  void tearDown() override {
    m_svc.reset();
  }

  void testRoundTripRegularUser() {
    auto user = makeUser("alice", "Alice Doe", false);
    const std::string token = m_svc->issue(user);
    auto claims = m_svc->verify(token);
    CPPUNIT_ASSERT(claims.has_value());
    CPPUNIT_ASSERT_EQUAL(std::string{"alice"}, claims->login);
    CPPUNIT_ASSERT_EQUAL(std::string{"Alice Doe"}, claims->fullName);
    CPPUNIT_ASSERT(!claims->isAdmin);
  }

  void testRoundTripAdminUser() {
    auto user = makeUser("bob", "Bob Admin", true);
    const std::string token = m_svc->issue(user);
    auto claims = m_svc->verify(token);
    CPPUNIT_ASSERT(claims.has_value());
    CPPUNIT_ASSERT_EQUAL(std::string{"bob"}, claims->login);
    CPPUNIT_ASSERT_EQUAL(std::string{"Bob Admin"}, claims->fullName);
    CPPUNIT_ASSERT(claims->isAdmin);
  }

  void testExpiredTokenRejected() {
    // Mint a token directly with exp already in the past — no sleep needed.
    const auto pastExp = std::chrono::system_clock::now() - std::chrono::seconds{10};
    const std::string expired = mintToken(m_secret, m_issuer, pastExp, "carol", "Carol", "user");
    auto claims = m_svc->verify(expired);
    CPPUNIT_ASSERT(!claims.has_value());
  }

  void testWrongSecretRejected() {
    const auto goodExp = std::chrono::system_clock::now() + std::chrono::seconds{3600};
    const std::string token =
      mintToken("different-secret-also-at-least-32-bytes!!", m_issuer, goodExp, "dave", "Dave", "user");
    auto claims = m_svc->verify(token);
    CPPUNIT_ASSERT(!claims.has_value());
  }

  void testTamperedTokenRejected() {
    auto user = makeUser("eve", "Eve", false);
    std::string token = m_svc->issue(user);
    // Flip one character somewhere in the payload part of the token.
    const auto dot1 = token.find('.');
    const auto dot2 = token.find('.', dot1 + 1);
    if (dot1 != std::string::npos && dot2 != std::string::npos && dot2 + 1 < token.size()) {
      token[dot2 + 1] ^= 1;
    }
    auto claims = m_svc->verify(token);
    CPPUNIT_ASSERT(!claims.has_value());
  }

  void testWrongIssuerRejected() {
    const auto goodExp = std::chrono::system_clock::now() + std::chrono::seconds{3600};
    const std::string token = mintToken(m_secret, "wrong-issuer", goodExp, "frank", "Frank", "user");
    auto claims = m_svc->verify(token);
    CPPUNIT_ASSERT(!claims.has_value());
  }

  void testGarbageInputRejected() {
    CPPUNIT_ASSERT(!m_svc->verify("not.a.jwt").has_value());
    CPPUNIT_ASSERT(!m_svc->verify("").has_value());
    CPPUNIT_ASSERT(!m_svc->verify("garbage!!@@##$$").has_value());
  }

  void testJtiUniquenessAcrossIssues() {
    // The spec (§5.3) requires a per-token random jti.  Two tokens issued for
    // the same user must differ — a proxy for jti entropy.
    auto user = makeUser("zara", "Zara Z", false);
    const std::string t1 = m_svc->issue(user);
    const std::string t2 = m_svc->issue(user);
    CPPUNIT_ASSERT(t1 != t2);
  }

private:
  std::string m_secret;
  std::string m_issuer;
  uint32_t m_ttl{0};
  std::unique_ptr<TokenService> m_svc;
};

CPPUNIT_TEST_SUITE_REGISTRATION(TokenServiceTest);

int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
