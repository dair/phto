#include <blob/Blob.h>
#include <metrics/Metrics.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <cstdint>
#include <vector>

using namespace blob;

// ============================================================================
// Default construction
// ============================================================================
class BlobDefaultConstructTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(BlobDefaultConstructTest);
  CPPUNIT_TEST(testDefaultIsEmpty);
  CPPUNIT_TEST(testDefaultSizeIsZero);
  CPPUNIT_TEST(testDefaultNotFrozen);
  CPPUNIT_TEST(testDefaultDataIsNull);
  CPPUNIT_TEST_SUITE_END();

public:
  void testDefaultIsEmpty() {
    Blob b;
    CPPUNIT_ASSERT(b.empty());
  }

  void testDefaultSizeIsZero() {
    Blob b;
    CPPUNIT_ASSERT_EQUAL(size_t(0), b.size());
  }

  void testDefaultNotFrozen() {
    Blob b;
    CPPUNIT_ASSERT(!b.frozen());
  }

  void testDefaultDataIsNull() {
    Blob b;
    CPPUNIT_ASSERT(b.data() == nullptr);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BlobDefaultConstructTest);

// ============================================================================
// Size construction
// ============================================================================
class BlobSizeConstructTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(BlobSizeConstructTest);
  CPPUNIT_TEST(testSizeIsSet);
  CPPUNIT_TEST(testNotFrozenInitially);
  CPPUNIT_TEST(testNotEmptyForPositiveSize);
  CPPUNIT_TEST(testWritableDataNonNull);
  CPPUNIT_TEST(testWritableDataCanBeWritten);
  CPPUNIT_TEST(testSizeZeroIsEmpty);
  CPPUNIT_TEST(testSizeZeroDataIsNull);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSizeIsSet() {
    Blob b(64);
    CPPUNIT_ASSERT_EQUAL(size_t(64), b.size());
  }

  void testNotFrozenInitially() {
    Blob b(16);
    CPPUNIT_ASSERT(!b.frozen());
  }

  void testNotEmptyForPositiveSize() {
    Blob b(1);
    CPPUNIT_ASSERT(!b.empty());
  }

  void testWritableDataNonNull() {
    Blob b(32);
    CPPUNIT_ASSERT(b.writableData() != nullptr);
  }

  void testWritableDataCanBeWritten() {
    Blob b(4);
    uint8_t *p = b.writableData();
    p[0] = 0xDE;
    p[1] = 0xAD;
    p[2] = 0xBE;
    p[3] = 0xEF;
    CPPUNIT_ASSERT_EQUAL(uint8_t(0xDE), b.data()[0]);
    CPPUNIT_ASSERT_EQUAL(uint8_t(0xEF), b.data()[3]);
  }

  void testSizeZeroIsEmpty() {
    Blob b(0);
    CPPUNIT_ASSERT(b.empty());
  }

  void testSizeZeroDataIsNull() {
    Blob b(0);
    CPPUNIT_ASSERT(b.data() == nullptr);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BlobSizeConstructTest);

// ============================================================================
// Freeze behaviour
// ============================================================================
class BlobFreezeTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(BlobFreezeTest);
  CPPUNIT_TEST(testFrozenAfterFreeze);
  CPPUNIT_TEST(testDataPointerUnchangedAfterFreeze);
  CPPUNIT_TEST(testNotEmptyAfterFreezeForPositiveSize);
  CPPUNIT_TEST(testDataReadableAfterFreeze);
  CPPUNIT_TEST_SUITE_END();

public:
  void testFrozenAfterFreeze() {
    Blob b(8);
    b.freeze();
    CPPUNIT_ASSERT(b.frozen());
  }

  void testDataPointerUnchangedAfterFreeze() {
    Blob b(8);
    const uint8_t *before = b.data();
    b.freeze();
    CPPUNIT_ASSERT_EQUAL(before, b.data());
  }

  void testNotEmptyAfterFreezeForPositiveSize() {
    Blob b(8);
    b.freeze();
    CPPUNIT_ASSERT(!b.empty());
  }

  void testDataReadableAfterFreeze() {
    Blob b(3);
    b.writableData()[0] = 10;
    b.writableData()[1] = 20;
    b.writableData()[2] = 30;
    b.freeze();
    CPPUNIT_ASSERT_EQUAL(uint8_t(10), b.data()[0]);
    CPPUNIT_ASSERT_EQUAL(uint8_t(20), b.data()[1]);
    CPPUNIT_ASSERT_EQUAL(uint8_t(30), b.data()[2]);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BlobFreezeTest);

// ============================================================================
// fromVector
// ============================================================================
class BlobFromVectorTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(BlobFromVectorTest);
  CPPUNIT_TEST(testSizeMatchesVector);
  CPPUNIT_TEST(testDataMatchesVector);
  CPPUNIT_TEST(testIsFrozen);
  CPPUNIT_TEST(testVectorMovedEmpty);
  CPPUNIT_TEST(testEmptyVectorReturnsEmptyBlob);
  CPPUNIT_TEST_SUITE_END();

public:
  void testSizeMatchesVector() {
    std::vector<uint8_t> v{1, 2, 3, 4, 5};
    Blob b = Blob::fromVector(std::move(v));
    CPPUNIT_ASSERT_EQUAL(size_t(5), b.size());
  }

  void testDataMatchesVector() {
    std::vector<uint8_t> v{0xAA, 0xBB, 0xCC};
    Blob b = Blob::fromVector(std::move(v));
    CPPUNIT_ASSERT_EQUAL(uint8_t(0xAA), b.data()[0]);
    CPPUNIT_ASSERT_EQUAL(uint8_t(0xBB), b.data()[1]);
    CPPUNIT_ASSERT_EQUAL(uint8_t(0xCC), b.data()[2]);
  }

  void testIsFrozen() {
    std::vector<uint8_t> v{1, 2, 3};
    Blob b = Blob::fromVector(std::move(v));
    CPPUNIT_ASSERT(b.frozen());
  }

  void testVectorMovedEmpty() {
    std::vector<uint8_t> v{10, 20, 30};
    Blob::fromVector(std::move(v));
    // After move, v should be empty (moved-from state)
    CPPUNIT_ASSERT(v.empty());
  }

  void testEmptyVectorReturnsEmptyBlob() {
    std::vector<uint8_t> v;
    Blob b = Blob::fromVector(std::move(v));
    CPPUNIT_ASSERT(b.empty());
    CPPUNIT_ASSERT_EQUAL(size_t(0), b.size());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BlobFromVectorTest);

// ============================================================================
// Shared ownership (copy of frozen blob)
// ============================================================================
class BlobSharedOwnershipTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(BlobSharedOwnershipTest);
  CPPUNIT_TEST(testCopiedBlobSameDataPointer);
  CPPUNIT_TEST(testCopiedBlobSameSize);
  CPPUNIT_TEST(testCopiedBlobBothFrozen);
  CPPUNIT_TEST_SUITE_END();

public:
  void testCopiedBlobSameDataPointer() {
    std::vector<uint8_t> v{1, 2, 3};
    Blob original = Blob::fromVector(std::move(v));
    // Copy via shared_ptr adoption constructor
    Blob copy = original;
    CPPUNIT_ASSERT_EQUAL(original.data(), copy.data());
  }

  void testCopiedBlobSameSize() {
    std::vector<uint8_t> v{1, 2, 3};
    Blob original = Blob::fromVector(std::move(v));
    Blob copy = original;
    CPPUNIT_ASSERT_EQUAL(original.size(), copy.size());
  }

  void testCopiedBlobBothFrozen() {
    std::vector<uint8_t> v{1, 2, 3};
    Blob original = Blob::fromVector(std::move(v));
    Blob copy = original;
    CPPUNIT_ASSERT(original.frozen());
    CPPUNIT_ASSERT(copy.frozen());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BlobSharedOwnershipTest);

// ============================================================================
// Metrics tracking
// ============================================================================
class BlobMetricsTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(BlobMetricsTest);
  CPPUNIT_TEST(testMetricsBlobsAliveIncrementsOnConstruct);
  CPPUNIT_TEST(testMetricsBlobBytesAliveIncrementsOnConstruct);
  CPPUNIT_TEST(testMetricsBlobsAliveDecrementsOnDestruct);
  CPPUNIT_TEST(testMetricsBlobBytesAliveDecrementsOnDestruct);
  CPPUNIT_TEST_SUITE_END();

  metrics::Metrics m_metrics;

public:
  void setUp() override {
    m_metrics.reset();
  }

  void testMetricsBlobsAliveIncrementsOnConstruct() {
    Blob b(128, &m_metrics);
    CPPUNIT_ASSERT_EQUAL(int64_t(1), m_metrics.blobs_alive.value());
  }

  void testMetricsBlobBytesAliveIncrementsOnConstruct() {
    Blob b(128, &m_metrics);
    CPPUNIT_ASSERT_EQUAL(int64_t(128), m_metrics.blob_bytes_alive.value());
  }

  void testMetricsBlobsAliveDecrementsOnDestruct() {
    {
      Blob b(64, &m_metrics);
      CPPUNIT_ASSERT_EQUAL(int64_t(1), m_metrics.blobs_alive.value());
    }
    CPPUNIT_ASSERT_EQUAL(int64_t(0), m_metrics.blobs_alive.value());
  }

  void testMetricsBlobBytesAliveDecrementsOnDestruct() {
    {
      Blob b(256, &m_metrics);
      CPPUNIT_ASSERT_EQUAL(int64_t(256), m_metrics.blob_bytes_alive.value());
    }
    CPPUNIT_ASSERT_EQUAL(int64_t(0), m_metrics.blob_bytes_alive.value());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(BlobMetricsTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry &reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
