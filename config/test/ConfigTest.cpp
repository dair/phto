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
class ValidConfigTest : public CppUnit::TestFixture {
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
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/images"),
                         cfg.targets[0].root.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/imager.db"),
                         cfg.targets[0].database.string());
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
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/images"),
                         cfg.targets[0].root.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk1/imager.db"),
                         cfg.targets[0].database.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk2/images"),
                         cfg.targets[1].root.string());
    CPPUNIT_ASSERT_EQUAL(std::string("/mnt/disk2/imager.db"),
                         cfg.targets[1].database.string());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ValidConfigTest);

// ============================================================================
// Error condition tests
// ============================================================================
class ErrorConfigTest : public CppUnit::TestFixture {
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
      f << "[[targets\n"   // unclosed bracket — malformed TOML
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
class SemanticValidationTest : public CppUnit::TestFixture {
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
        << "root = \"/mnt/disk1/images\"\n"   // same root as first target
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
        << "database = \"/mnt/shared/imager.db\"\n";  // same database as first target
    }
    CPPUNIT_ASSERT_THROW(loadConfig(m_path), std::runtime_error);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SemanticValidationTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
