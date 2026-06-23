#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "config/Config.h"

namespace fs = std::filesystem;
using config::loadConfig;

// ---------------------------------------------------------------------------
// Helper: unique temp path for a TOML config file
// ---------------------------------------------------------------------------
static fs::path tempTomlPath(const std::string& suffix = "") {
  return fs::temp_directory_path() / ("config_test_" + suffix + ".toml");
}

// ============================================================================
// Valid config parsing tests
// ============================================================================
class ValidConfigTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ValidConfigTest);
  CPPUNIT_TEST(testSingleTarget);
  CPPUNIT_TEST(testTwoTargets);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

public:
  void setUp() override {
    m_path = tempTomlPath("valid");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testSingleTarget() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n";
    }

    auto cfg = loadConfig(m_path);
    CPPUNIT_ASSERT_EQUAL(size_t(1), cfg.targets.size());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/images"), cfg.targets[0].root.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/imager.db"), cfg.targets[0].database.string());
  }

  void testTwoTargets() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n"
        << "\n"
        << "[[targets]]\n"
        << "root = \"/mnt/disk2/images\"\n"
        << "database = \"/mnt/disk2/imager.db\"\n";
    }

    auto cfg = loadConfig(m_path);
    CPPUNIT_ASSERT_EQUAL(size_t(2), cfg.targets.size());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/images"), cfg.targets[0].root.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/imager.db"), cfg.targets[0].database.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk2/images"), cfg.targets[1].root.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk2/imager.db"), cfg.targets[1].database.string());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ValidConfigTest);

// ============================================================================
// Error condition tests
// ============================================================================
class ErrorConfigTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ErrorConfigTest);
  CPPUNIT_TEST(testMissingFile);
  CPPUNIT_TEST(testEmptyFile);
  CPPUNIT_TEST(testMalformedToml);
  CPPUNIT_TEST(testMissingRootField);
  CPPUNIT_TEST(testMissingDatabaseField);
  CPPUNIT_TEST(testEmptyTargetsArray);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

public:
  void setUp() override {
    m_path = tempTomlPath("error");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testMissingFile() {
    fs::path nonexistent("/tmp/nonexistent_config_xyz.toml");
    CPPUNIT_ASSERT(!fs::exists(nonexistent));
    CPPUNIT_ASSERT_THROW(loadConfig(nonexistent), std::runtime_error);
  }

  void testEmptyFile() {
    // Create an empty file — no [[targets]] present
    { std::ofstream f(m_path); }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testMalformedToml() {
    {
      std::ofstream f(m_path);
      f << "[[targets\n" // unclosed bracket — malformed TOML
        << "root = \"/mnt/disk1\"\n";
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testMissingRootField() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "database = \"/mnt/disk1/imager.db\"\n";
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testMissingDatabaseField() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n";
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testEmptyTargetsArray() {
    // 'targets' as a plain key with an empty inline array, not [[targets]] entries
    {
      std::ofstream f(m_path);
      f << "targets = []\n";
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ErrorConfigTest);

// ============================================================================
// Semantic validation tests (duplicate root / database)
// ============================================================================
class SemanticValidationTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SemanticValidationTest);
  CPPUNIT_TEST(testDuplicateRootPaths);
  CPPUNIT_TEST(testDuplicateDatabasePaths);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

public:
  void setUp() override {
    m_path = tempTomlPath("semantic");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testDuplicateRootPaths() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n"
        << "\n"
        << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n" // same root as first target
        << "database = \"/mnt/disk2/imager.db\"\n";
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testDuplicateDatabasePaths() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/shared/imager.db\"\n"
        << "\n"
        << "[[targets]]\n"
        << "root = \"/mnt/disk2/images\"\n"
        << "database = \"/mnt/shared/imager.db\"\n"; // same database as first target
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SemanticValidationTest);

// ============================================================================
// ServerConfig / AuthConfig — backward compatibility (no sections present)
// ============================================================================
class ServerAuthDefaultsTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ServerAuthDefaultsTest);
  CPPUNIT_TEST(testNoServerOrAuthSections);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

public:
  void setUp() override {
    m_path = tempTomlPath("serverauth_defaults");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testNoServerOrAuthSections() {
    // A minimal config with only [[targets]] — no [server] or [auth] — must
    // still load fine and expose default-valued structs.
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n";
    }
    auto cfg = loadConfig(m_path);

    // server defaults
    CPPUNIT_ASSERT_EQUAL(std::string("0.0.0.0"), cfg.server.bind);
    CPPUNIT_ASSERT_EQUAL(uint16_t(8443), cfg.server.port);
    CPPUNIT_ASSERT_EQUAL(false, cfg.server.tls);
    CPPUNIT_ASSERT(cfg.server.tlsCert.empty());
    CPPUNIT_ASSERT(cfg.server.tlsKey.empty());
    CPPUNIT_ASSERT_EQUAL(uint32_t(0), cfg.server.threads);
    CPPUNIT_ASSERT_EQUAL(uint64_t(4ULL * 1024 * 1024 * 1024), cfg.server.maxUploadBytes);

    // auth defaults
    CPPUNIT_ASSERT(cfg.auth.database.empty());
    CPPUNIT_ASSERT(cfg.auth.jwtSecret.empty());
    CPPUNIT_ASSERT(cfg.auth.jwtSecretFile.empty());
    CPPUNIT_ASSERT_EQUAL(uint32_t(43200), cfg.auth.tokenTtlSeconds);
    CPPUNIT_ASSERT_EQUAL(std::string("phto"), cfg.auth.issuer);
    CPPUNIT_ASSERT_EQUAL(uint32_t(310000), cfg.auth.pbkdf2Iterations);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ServerAuthDefaultsTest);

// ============================================================================
// ServerConfig / AuthConfig — fully-populated sections
// ============================================================================
class ServerAuthFullTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ServerAuthFullTest);
  CPPUNIT_TEST(testFullServerAndAuth);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

public:
  void setUp() override {
    m_path = tempTomlPath("serverauth_full");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testFullServerAndAuth() {
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n"
        << "\n"
        << "[server]\n"
        << "bind = \"127.0.0.1\"\n"
        << "port = 9000\n"
        << "tls = true\n"
        << "tls_cert = \"/etc/phto/tls/fullchain.pem\"\n"
        << "tls_key = \"/etc/phto/tls/privkey.pem\"\n"
        << "threads = 4\n"
        << "max_upload_mb = 2048\n"
        << "\n"
        << "[auth]\n"
        << "database = \"/var/lib/phto/auth.db\"\n"
        << "jwt_secret = \"mysecretkey\"\n"
        << "jwt_secret_file = \"/etc/phto/jwt.secret\"\n"
        << "token_ttl_seconds = 3600\n"
        << "issuer = \"myapp\"\n"
        << "pbkdf2_iterations = 500000\n";
    }
    auto cfg = loadConfig(m_path);

    // server
    CPPUNIT_ASSERT_EQUAL(std::string("127.0.0.1"), cfg.server.bind);
    CPPUNIT_ASSERT_EQUAL(uint16_t(9000), cfg.server.port);
    CPPUNIT_ASSERT_EQUAL(true, cfg.server.tls);
    CPPUNIT_ASSERT_EQUAL(std::string("/etc/phto/tls/fullchain.pem"), cfg.server.tlsCert.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/etc/phto/tls/privkey.pem"), cfg.server.tlsKey.string());
    CPPUNIT_ASSERT_EQUAL(uint32_t(4), cfg.server.threads);
    // 2048 MB -> bytes
    CPPUNIT_ASSERT_EQUAL(uint64_t(2048ULL * 1024 * 1024), cfg.server.maxUploadBytes);

    // auth
    CPPUNIT_ASSERT_EQUAL(std::string("/var/lib/phto/auth.db"), cfg.auth.database.string());
    CPPUNIT_ASSERT_EQUAL(std::string("mysecretkey"), cfg.auth.jwtSecret);
    CPPUNIT_ASSERT_EQUAL(std::string("/etc/phto/jwt.secret"), cfg.auth.jwtSecretFile.string());
    CPPUNIT_ASSERT_EQUAL(uint32_t(3600), cfg.auth.tokenTtlSeconds);
    CPPUNIT_ASSERT_EQUAL(std::string("myapp"), cfg.auth.issuer);
    CPPUNIT_ASSERT_EQUAL(uint32_t(500000), cfg.auth.pbkdf2Iterations);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ServerAuthFullTest);

// ============================================================================
// ServerConfig / AuthConfig — partial sections (missing keys keep defaults)
// ============================================================================
class ServerAuthPartialTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ServerAuthPartialTest);
  CPPUNIT_TEST(testPartialServer);
  CPPUNIT_TEST(testPartialAuth);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

public:
  void setUp() override {
    m_path = tempTomlPath("serverauth_partial");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testPartialServer() {
    // Only 'port' overridden in [server]; others keep defaults.
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n"
        << "\n"
        << "[server]\n"
        << "port = 443\n";
    }
    auto cfg = loadConfig(m_path);
    CPPUNIT_ASSERT_EQUAL(uint16_t(443), cfg.server.port);
    // Others still at defaults
    CPPUNIT_ASSERT_EQUAL(std::string("0.0.0.0"), cfg.server.bind);
    CPPUNIT_ASSERT_EQUAL(false, cfg.server.tls);
    CPPUNIT_ASSERT_EQUAL(uint32_t(0), cfg.server.threads);
    CPPUNIT_ASSERT_EQUAL(uint64_t(4ULL * 1024 * 1024 * 1024), cfg.server.maxUploadBytes);
  }

  void testPartialAuth() {
    // Only 'issuer' and 'token_ttl_seconds' overridden in [auth].
    {
      std::ofstream f(m_path);
      f << "[[targets]]\n"
        << "root = \"/mnt/disk1/images\"\n"
        << "database = \"/mnt/disk1/imager.db\"\n"
        << "\n"
        << "[auth]\n"
        << "issuer = \"custom\"\n"
        << "token_ttl_seconds = 7200\n";
    }
    auto cfg = loadConfig(m_path);
    CPPUNIT_ASSERT_EQUAL(std::string("custom"), cfg.auth.issuer);
    CPPUNIT_ASSERT_EQUAL(uint32_t(7200), cfg.auth.tokenTtlSeconds);
    // Others still at defaults
    CPPUNIT_ASSERT(cfg.auth.database.empty());
    CPPUNIT_ASSERT(cfg.auth.jwtSecret.empty());
    CPPUNIT_ASSERT_EQUAL(uint32_t(310000), cfg.auth.pbkdf2Iterations);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ServerAuthPartialTest);

// ============================================================================
// ServerConfig / AuthConfig — invalid values throw std::runtime_error
// ============================================================================
class ServerAuthValidationTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ServerAuthValidationTest);
  CPPUNIT_TEST(testPortZero);
  CPPUNIT_TEST(testPortTooLarge);
  CPPUNIT_TEST(testPortTypeMismatch);
  CPPUNIT_TEST(testMaxUploadMbZero);
  CPPUNIT_TEST(testTokenTtlZero);
  CPPUNIT_TEST(testPbkdf2IterationsTooLow);
  CPPUNIT_TEST_SUITE_END();

  fs::path m_path;

  void writeConfig(const std::string& section) {
    std::ofstream f(m_path);
    f << "[[targets]]\n"
      << "root = \"/mnt/disk1/images\"\n"
      << "database = \"/mnt/disk1/imager.db\"\n"
      << "\n"
      << section;
  }

public:
  void setUp() override {
    m_path = tempTomlPath("serverauth_validation");
  }

  void tearDown() override {
    fs::remove(m_path);
  }

  void testPortZero() {
    writeConfig("[server]\nport = 0\n");
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testPortTooLarge() {
    writeConfig("[server]\nport = 70000\n");
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testPortTypeMismatch() {
    writeConfig("[server]\nport = \"abc\"\n");
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testMaxUploadMbZero() {
    writeConfig("[server]\nmax_upload_mb = 0\n");
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testTokenTtlZero() {
    writeConfig("[auth]\ntoken_ttl_seconds = 0\n");
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }

  void testPbkdf2IterationsTooLow() {
    writeConfig("[auth]\npbkdf2_iterations = 10\n");
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ServerAuthValidationTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
