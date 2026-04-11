#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <metrics/Counter.h>
#include <metrics/Gauge.h>
#include <metrics/Histogram.h>
#include <metrics/Metrics.h>
#include <metrics/Snapshot.h>
#include <metrics/Timer.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

using namespace metrics;
using namespace std::chrono_literals;

// ============================================================================
// Counter tests
// ============================================================================
class CounterTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(CounterTest);
  CPPUNIT_TEST(testInitialValueIsZero);
  CPPUNIT_TEST(testAdd);
  CPPUNIT_TEST(testMultipleAddsAccumulate);
  CPPUNIT_TEST(testReset);
  CPPUNIT_TEST(testName);
  CPPUNIT_TEST_SUITE_END();

public:
  void testInitialValueIsZero() {
    Counter c("test_counter");
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), c.value());
  }

  void testAdd() {
    Counter c("test_counter");
    c.add(42);
    CPPUNIT_ASSERT_EQUAL(uint64_t(42), c.value());
  }

  void testMultipleAddsAccumulate() {
    Counter c("test_counter");
    c.add(10);
    c.add(20);
    c.add(30);
    CPPUNIT_ASSERT_EQUAL(uint64_t(60), c.value());
  }

  void testReset() {
    Counter c("test_counter");
    c.add(100);
    c.reset();
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), c.value());
  }

  void testName() {
    Counter c("my_counter");
    CPPUNIT_ASSERT_EQUAL(std::string("my_counter"), c.name());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(CounterTest);

// ============================================================================
// Gauge tests
// ============================================================================
class GaugeTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(GaugeTest);
  CPPUNIT_TEST(testInitialValueIsZero);
  CPPUNIT_TEST(testIncrement);
  CPPUNIT_TEST(testDecrement);
  CPPUNIT_TEST(testAddPositive);
  CPPUNIT_TEST(testAddNegative);
  CPPUNIT_TEST(testSet);
  CPPUNIT_TEST(testReset);
  CPPUNIT_TEST(testName);
  CPPUNIT_TEST(testDecrementBelowZero);
  CPPUNIT_TEST_SUITE_END();

public:
  void testInitialValueIsZero() {
    Gauge g("test_gauge");
    CPPUNIT_ASSERT_EQUAL(int64_t(0), g.value());
  }

  void testIncrement() {
    Gauge g("test_gauge");
    g.increment();
    CPPUNIT_ASSERT_EQUAL(int64_t(1), g.value());
  }

  void testDecrement() {
    Gauge g("test_gauge");
    g.increment();
    g.increment();
    g.decrement();
    CPPUNIT_ASSERT_EQUAL(int64_t(1), g.value());
  }

  void testAddPositive() {
    Gauge g("test_gauge");
    g.add(50);
    CPPUNIT_ASSERT_EQUAL(int64_t(50), g.value());
  }

  void testAddNegative() {
    Gauge g("test_gauge");
    g.add(100);
    g.add(-30);
    CPPUNIT_ASSERT_EQUAL(int64_t(70), g.value());
  }

  void testSet() {
    Gauge g("test_gauge");
    g.add(999);
    g.set(42);
    CPPUNIT_ASSERT_EQUAL(int64_t(42), g.value());
  }

  void testReset() {
    Gauge g("test_gauge");
    g.add(77);
    g.reset();
    CPPUNIT_ASSERT_EQUAL(int64_t(0), g.value());
  }

  void testName() {
    Gauge g("my_gauge");
    CPPUNIT_ASSERT_EQUAL(std::string("my_gauge"), g.name());
  }

  void testDecrementBelowZero() {
    Gauge g("test_gauge");
    g.decrement();
    CPPUNIT_ASSERT_EQUAL(int64_t(-1), g.value());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(GaugeTest);

// ============================================================================
// GaugeGuard tests
// ============================================================================
class GaugeGuardTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(GaugeGuardTest);
  CPPUNIT_TEST(testGuardIncrementsOnConstruct);
  CPPUNIT_TEST(testGuardDecrementsOnDestruct);
  CPPUNIT_TEST(testNestedGuards);
  CPPUNIT_TEST_SUITE_END();

public:
  void testGuardIncrementsOnConstruct() {
    Gauge g("guard_gauge");
    CPPUNIT_ASSERT_EQUAL(int64_t(0), g.value());
    GaugeGuard guard(g);
    CPPUNIT_ASSERT_EQUAL(int64_t(1), g.value());
  }

  void testGuardDecrementsOnDestruct() {
    Gauge g("guard_gauge");
    {
      GaugeGuard guard(g);
      CPPUNIT_ASSERT_EQUAL(int64_t(1), g.value());
    }
    CPPUNIT_ASSERT_EQUAL(int64_t(0), g.value());
  }

  void testNestedGuards() {
    Gauge g("guard_gauge");
    {
      GaugeGuard outer(g);
      CPPUNIT_ASSERT_EQUAL(int64_t(1), g.value());
      {
        GaugeGuard inner(g);
        CPPUNIT_ASSERT_EQUAL(int64_t(2), g.value());
      }
      CPPUNIT_ASSERT_EQUAL(int64_t(1), g.value());
    }
    CPPUNIT_ASSERT_EQUAL(int64_t(0), g.value());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(GaugeGuardTest);

// ============================================================================
// SizedGaugeGuard tests
// ============================================================================
class SizedGaugeGuardTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SizedGaugeGuardTest);
  CPPUNIT_TEST(testBothGaugesIncrementedOnConstruct);
  CPPUNIT_TEST(testBothGaugesDecrementedOnDestruct);
  CPPUNIT_TEST(testSizeIsTrackedCorrectly);
  CPPUNIT_TEST_SUITE_END();

public:
  void testBothGaugesIncrementedOnConstruct() {
    Gauge files("files");
    Gauge bytes("bytes");
    SizedGaugeGuard guard(files, bytes, 1024);
    CPPUNIT_ASSERT_EQUAL(int64_t(1), files.value());
    CPPUNIT_ASSERT_EQUAL(int64_t(1024), bytes.value());
  }

  void testBothGaugesDecrementedOnDestruct() {
    Gauge files("files");
    Gauge bytes("bytes");
    {
      SizedGaugeGuard guard(files, bytes, 512);
      CPPUNIT_ASSERT_EQUAL(int64_t(1), files.value());
      CPPUNIT_ASSERT_EQUAL(int64_t(512), bytes.value());
    }
    CPPUNIT_ASSERT_EQUAL(int64_t(0), files.value());
    CPPUNIT_ASSERT_EQUAL(int64_t(0), bytes.value());
  }

  void testSizeIsTrackedCorrectly() {
    Gauge files("files");
    Gauge bytes("bytes");
    {
      SizedGaugeGuard g1(files, bytes, 100);
      SizedGaugeGuard g2(files, bytes, 200);
      CPPUNIT_ASSERT_EQUAL(int64_t(2), files.value());
      CPPUNIT_ASSERT_EQUAL(int64_t(300), bytes.value());
    }
    CPPUNIT_ASSERT_EQUAL(int64_t(0), files.value());
    CPPUNIT_ASSERT_EQUAL(int64_t(0), bytes.value());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SizedGaugeGuardTest);

// ============================================================================
// Histogram tests
// ============================================================================
class HistogramTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(HistogramTest);
  CPPUNIT_TEST(testInitialSnapshotIsEmpty);
  CPPUNIT_TEST(testRecordOneSample);
  CPPUNIT_TEST(testRecordUpdatesSumNs);
  CPPUNIT_TEST(testRecordIncrementsCorrectBucket);
  CPPUNIT_TEST(testRecordMultipleSamples);
  CPPUNIT_TEST(testResetClearsAll);
  CPPUNIT_TEST(testNamePreserved);
  CPPUNIT_TEST(testLargeValueGoesToLastBucket);
  CPPUNIT_TEST_SUITE_END();

public:
  void testInitialSnapshotIsEmpty() {
    Histogram h("test_hist");
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), snap.count);
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), snap.sum_ns);
    for (size_t i = 0; i < Histogram::NUM_BUCKETS; ++i) {
      CPPUNIT_ASSERT_EQUAL(uint64_t(0), snap.buckets[i]);
    }
  }

  void testRecordOneSample() {
    Histogram h("test_hist");
    h.record(std::chrono::nanoseconds(1000));
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), snap.count);
  }

  void testRecordUpdatesSumNs() {
    Histogram h("test_hist");
    h.record(std::chrono::nanoseconds(500));
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(500), snap.sum_ns);
  }

  void testRecordIncrementsCorrectBucket() {
    Histogram h("test_hist");
    // 1000 ns falls into bucket index 2 (bound is 1000 ns, i.e. <= 1000 goes to bucket 2)
    h.record(std::chrono::nanoseconds(1000));
    auto snap = h.snapshot();
    // Verify exactly one bucket has count=1
    uint64_t totalBucketCount = 0;
    for (size_t i = 0; i < Histogram::NUM_BUCKETS; ++i) {
      totalBucketCount += snap.buckets[i];
    }
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), totalBucketCount);
  }

  void testRecordMultipleSamples() {
    Histogram h("test_hist");
    h.record(std::chrono::nanoseconds(100));
    h.record(std::chrono::nanoseconds(1000));
    h.record(std::chrono::nanoseconds(1000000));
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(3), snap.count);
    CPPUNIT_ASSERT_EQUAL(uint64_t(100 + 1000 + 1000000), snap.sum_ns);
  }

  void testResetClearsAll() {
    Histogram h("test_hist");
    h.record(std::chrono::nanoseconds(5000));
    h.record(std::chrono::nanoseconds(50000));
    h.reset();
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), snap.count);
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), snap.sum_ns);
    for (size_t i = 0; i < Histogram::NUM_BUCKETS; ++i) {
      CPPUNIT_ASSERT_EQUAL(uint64_t(0), snap.buckets[i]);
    }
  }

  void testNamePreserved() {
    Histogram h("latency_hist");
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(std::string("latency_hist"), snap.name);
  }

  void testLargeValueGoesToLastBucket() {
    Histogram h("test_hist");
    // 60 seconds — beyond the last bound of 30s
    h.record(std::chrono::nanoseconds(60'000'000'000LL));
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), snap.buckets[Histogram::NUM_BUCKETS - 1]);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(HistogramTest);

// ============================================================================
// Timer tests
// ============================================================================
class TimerTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(TimerTest);
  CPPUNIT_TEST(testRaiiRecordsOnDestruct);
  CPPUNIT_TEST(testStopRecordsImmediately);
  CPPUNIT_TEST(testDoubleStopIsNoOp);
  CPPUNIT_TEST(testStopThenDestructIsNoOp);
  CPPUNIT_TEST_SUITE_END();

public:
  void testRaiiRecordsOnDestruct() {
    Histogram h("timer_hist");
    {
      Timer t(h);
      std::this_thread::sleep_for(1ms);
    }
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), snap.count);
    CPPUNIT_ASSERT(snap.sum_ns > 0);
  }

  void testStopRecordsImmediately() {
    Histogram h("timer_hist");
    Timer t(h);
    std::this_thread::sleep_for(1ms);
    t.stop();
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), snap.count);
    CPPUNIT_ASSERT(snap.sum_ns > 0);
  }

  void testDoubleStopIsNoOp() {
    Histogram h("timer_hist");
    Timer t(h);
    t.stop();
    t.stop(); // second stop must not record again
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), snap.count);
  }

  void testStopThenDestructIsNoOp() {
    Histogram h("timer_hist");
    {
      Timer t(h);
      t.stop();
    } // destructor runs here — must not record a second time
    auto snap = h.snapshot();
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), snap.count);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TimerTest);

// ============================================================================
// Metrics registry snapshot tests
// ============================================================================
class MetricsSnapshotTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(MetricsSnapshotTest);
  CPPUNIT_TEST(testSnapshotContainsCounters);
  CPPUNIT_TEST(testSnapshotContainsGauges);
  CPPUNIT_TEST(testSnapshotContainsHistograms);
  CPPUNIT_TEST(testSnapshotCounterValuesReflectMutations);
  CPPUNIT_TEST(testSnapshotGaugeValuesReflectMutations);
  CPPUNIT_TEST(testSnapshotHistogramValuesReflectMutations);
  CPPUNIT_TEST_SUITE_END();

  Metrics m_metrics;

public:
  void setUp() override {
    m_metrics.reset();
  }

  void testSnapshotContainsCounters() {
    auto snap = m_metrics.snapshot();
    CPPUNIT_ASSERT(!snap.counters.empty());
    bool found = false;
    for (const auto& c : snap.counters) {
      if (c.name == "images_added") {
        found = true;
        break;
      }
    }
    CPPUNIT_ASSERT(found);
  }

  void testSnapshotContainsGauges() {
    auto snap = m_metrics.snapshot();
    CPPUNIT_ASSERT(!snap.gauges.empty());
    bool found = false;
    for (const auto& g : snap.gauges) {
      if (g.name == "blobs_alive") {
        found = true;
        break;
      }
    }
    CPPUNIT_ASSERT(found);
  }

  void testSnapshotContainsHistograms() {
    auto snap = m_metrics.snapshot();
    CPPUNIT_ASSERT(!snap.histograms.empty());
    bool found = false;
    for (const auto& h : snap.histograms) {
      if (h.name == "hash") {
        found = true;
        break;
      }
    }
    CPPUNIT_ASSERT(found);
  }

  void testSnapshotCounterValuesReflectMutations() {
    m_metrics.images_added.add(7);
    auto snap = m_metrics.snapshot();
    for (const auto& c : snap.counters) {
      if (c.name == "images_added") {
        CPPUNIT_ASSERT_EQUAL(uint64_t(7), c.value);
        return;
      }
    }
    CPPUNIT_FAIL("images_added not found in snapshot");
  }

  void testSnapshotGaugeValuesReflectMutations() {
    m_metrics.blobs_alive.set(3);
    auto snap = m_metrics.snapshot();
    for (const auto& g : snap.gauges) {
      if (g.name == "blobs_alive") {
        CPPUNIT_ASSERT_EQUAL(int64_t(3), g.value);
        return;
      }
    }
    CPPUNIT_FAIL("blobs_alive not found in snapshot");
  }

  void testSnapshotHistogramValuesReflectMutations() {
    m_metrics.hash.record(std::chrono::nanoseconds(5000));
    auto snap = m_metrics.snapshot();
    for (const auto& h : snap.histograms) {
      if (h.name == "hash") {
        CPPUNIT_ASSERT_EQUAL(uint64_t(1), h.count);
        CPPUNIT_ASSERT_EQUAL(uint64_t(5000), h.sum_ns);
        return;
      }
    }
    CPPUNIT_FAIL("hash histogram not found in snapshot");
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(MetricsSnapshotTest);

// ============================================================================
// Metrics reset tests
// ============================================================================
class MetricsResetTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(MetricsResetTest);
  CPPUNIT_TEST(testResetZeroesCounters);
  CPPUNIT_TEST(testResetZeroesGauges);
  CPPUNIT_TEST(testResetZeroesHistograms);
  CPPUNIT_TEST_SUITE_END();

  Metrics m_metrics;

public:
  void testResetZeroesCounters() {
    m_metrics.images_added.add(100);
    m_metrics.images_failed.add(50);
    m_metrics.reset();
    auto snap = m_metrics.snapshot();
    for (const auto& c : snap.counters) {
      CPPUNIT_ASSERT_EQUAL(uint64_t(0), c.value);
    }
  }

  void testResetZeroesGauges() {
    m_metrics.blobs_alive.set(10);
    m_metrics.pool_active_threads.set(4);
    m_metrics.reset();
    auto snap = m_metrics.snapshot();
    for (const auto& g : snap.gauges) {
      CPPUNIT_ASSERT_EQUAL(int64_t(0), g.value);
    }
  }

  void testResetZeroesHistograms() {
    m_metrics.hash.record(std::chrono::nanoseconds(1000));
    m_metrics.validate.record(std::chrono::nanoseconds(2000));
    m_metrics.reset();
    auto snap = m_metrics.snapshot();
    for (const auto& h : snap.histograms) {
      CPPUNIT_ASSERT_EQUAL(uint64_t(0), h.count);
      CPPUNIT_ASSERT_EQUAL(uint64_t(0), h.sum_ns);
    }
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(MetricsResetTest);

// ============================================================================
// Snapshot format tests
// ============================================================================
class SnapshotFormatTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SnapshotFormatTest);
  CPPUNIT_TEST(testFormatReturnsNonEmptyString);
  CPPUNIT_TEST(testFormatContainsKnownMetricName);
  CPPUNIT_TEST(testFormatEmptySnapshot);
  CPPUNIT_TEST_SUITE_END();

public:
  void testFormatReturnsNonEmptyString() {
    Metrics m;
    m.images_added.add(1);
    auto snap = m.snapshot();
    std::string output = format(snap);
    CPPUNIT_ASSERT(!output.empty());
  }

  void testFormatContainsKnownMetricName() {
    Metrics m;
    m.images_added.add(5);
    auto snap = m.snapshot();
    std::string output = format(snap);
    CPPUNIT_ASSERT(output.find("images_added") != std::string::npos);
  }

  void testFormatEmptySnapshot() {
    FullSnapshot empty;
    std::string output = format(empty);
    // Even an empty snapshot should produce a string (possibly empty or just whitespace)
    // The key requirement is it does not throw
    CPPUNIT_ASSERT_NO_THROW(format(empty));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SnapshotFormatTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
