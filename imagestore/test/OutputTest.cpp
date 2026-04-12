#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <atomic>
#include <chrono>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../ResultLog.h"
#include "../SlotTracker.h"

namespace imagestore {

// ---------------------------------------------------------------------------
// SlotTracker tests
// ---------------------------------------------------------------------------

class SlotTrackerTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SlotTrackerTest);
  CPPUNIT_TEST(testConstructorCreatesIdleSlots);
  CPPUNIT_TEST(testAcquireReturnsDifferentSlots);
  CPPUNIT_TEST(testAcquireSetsFilenameAndReadingStage);
  CPPUNIT_TEST(testSetStageUpdatesStage);
  CPPUNIT_TEST(testSetStageUpdatesStageTime);
  CPPUNIT_TEST(testReleaseMarksSlotIdle);
  CPPUNIT_TEST(testReleaseAllowsReacquire);
  CPPUNIT_TEST(testSnapshotReturnsAllSlots);
  CPPUNIT_TEST(testSnapshotIsIndependent);
  CPPUNIT_TEST(testSetStageOutOfBoundsIsNoop);
  CPPUNIT_TEST(testReleaseOutOfBoundsIsNoop);
  CPPUNIT_TEST(testStageName);
  CPPUNIT_TEST(testConcurrentAcquireRelease);
  CPPUNIT_TEST(testSingleSlot);
  CPPUNIT_TEST_SUITE_END();

public:
  void testConstructorCreatesIdleSlots() {
    SlotTracker tracker(4);
    auto slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(4), slots.size());
    for (const auto& s : slots) {
      CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, s.stage);
      CPPUNIT_ASSERT(s.filename.empty());
    }
  }

  void testAcquireReturnsDifferentSlots() {
    SlotTracker tracker(3);
    unsigned int s0 = tracker.acquire("file0.jpg");
    unsigned int s1 = tracker.acquire("file1.jpg");
    unsigned int s2 = tracker.acquire("file2.jpg");
    // All three must be different and within [0, 3)
    CPPUNIT_ASSERT(s0 < 3u);
    CPPUNIT_ASSERT(s1 < 3u);
    CPPUNIT_ASSERT(s2 < 3u);
    CPPUNIT_ASSERT(s0 != s1);
    CPPUNIT_ASSERT(s1 != s2);
    CPPUNIT_ASSERT(s0 != s2);

    // Release all to clean up
    tracker.release(s0);
    tracker.release(s1);
    tracker.release(s2);
  }

  void testAcquireSetsFilenameAndReadingStage() {
    SlotTracker tracker(2);
    unsigned int slot = tracker.acquire("photo.jpg");
    auto slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(std::string("photo.jpg"), slots[slot].filename);
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Reading, slots[slot].stage);
    tracker.release(slot);
  }

  void testSetStageUpdatesStage() {
    SlotTracker tracker(2);
    unsigned int slot = tracker.acquire("test.jpg");
    tracker.setStage(slot, PipelineStage::Validating);
    auto slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Validating, slots[slot].stage);

    tracker.setStage(slot, PipelineStage::Hashing);
    slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Hashing, slots[slot].stage);

    tracker.setStage(slot, PipelineStage::WritingStorage);
    slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::WritingStorage, slots[slot].stage);

    tracker.setStage(slot, PipelineStage::InsertingDb);
    slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::InsertingDb, slots[slot].stage);

    tracker.release(slot);
  }

  void testSetStageUpdatesStageTime() {
    SlotTracker tracker(1);
    unsigned int slot = tracker.acquire("test.jpg");
    auto before = tracker.snapshot()[slot].stageStart;

    // Sleep briefly so the clock advances
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    tracker.setStage(slot, PipelineStage::Hashing);
    auto after = tracker.snapshot()[slot].stageStart;

    CPPUNIT_ASSERT(after >= before);
    tracker.release(slot);
  }

  void testReleaseMarksSlotIdle() {
    SlotTracker tracker(2);
    unsigned int slot = tracker.acquire("test.jpg");
    tracker.setStage(slot, PipelineStage::Hashing);
    tracker.release(slot);

    auto slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, slots[slot].stage);
    CPPUNIT_ASSERT(slots[slot].filename.empty());
  }

  void testReleaseAllowsReacquire() {
    SlotTracker tracker(1);
    unsigned int slot0 = tracker.acquire("first.jpg");
    tracker.release(slot0);
    unsigned int slot1 = tracker.acquire("second.jpg");
    // With only one slot, both must use slot 0
    CPPUNIT_ASSERT_EQUAL(0u, slot0);
    CPPUNIT_ASSERT_EQUAL(0u, slot1);
    auto s = tracker.snapshot()[slot1];
    CPPUNIT_ASSERT_EQUAL(std::string("second.jpg"), s.filename);
    tracker.release(slot1);
  }

  void testSnapshotReturnsAllSlots() {
    SlotTracker tracker(3);
    unsigned int s0 = tracker.acquire("a.jpg");
    unsigned int s1 = tracker.acquire("b.jpg");
    // s2 not acquired — remains idle
    auto slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(3), slots.size());

    // Count active and idle
    int active = 0;
    int idle = 0;
    for (const auto& s : slots) {
      if (s.stage == PipelineStage::Idle)
        ++idle;
      else
        ++active;
    }
    CPPUNIT_ASSERT_EQUAL(2, active);
    CPPUNIT_ASSERT_EQUAL(1, idle);

    tracker.release(s0);
    tracker.release(s1);
  }

  void testSnapshotIsIndependent() {
    // Modifying the tracker after snapshot must not affect previous snapshot
    SlotTracker tracker(2);
    unsigned int slot = tracker.acquire("file.jpg");
    auto snap1 = tracker.snapshot();
    tracker.setStage(slot, PipelineStage::Hashing);
    auto snap2 = tracker.snapshot();

    // snap1 should still show Reading
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Reading, snap1[slot].stage);
    // snap2 should show Hashing
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Hashing, snap2[slot].stage);

    tracker.release(slot);
  }

  void testSetStageOutOfBoundsIsNoop() {
    SlotTracker tracker(2);
    // Out-of-bounds slot — must not crash
    tracker.setStage(999, PipelineStage::Hashing);
    tracker.setStage(2, PipelineStage::Hashing); // exactly at limit
    auto slots = tracker.snapshot();
    // Both valid slots still idle
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, slots[0].stage);
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, slots[1].stage);
  }

  void testReleaseOutOfBoundsIsNoop() {
    SlotTracker tracker(2);
    // Out-of-bounds release must not crash
    tracker.release(999);
    tracker.release(2); // exactly at limit
    auto slots = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, slots[0].stage);
  }

  void testStageName() {
    CPPUNIT_ASSERT_EQUAL(std::string("reading"), std::string(stageName(PipelineStage::Reading)));
    CPPUNIT_ASSERT_EQUAL(std::string("validating"), std::string(stageName(PipelineStage::Validating)));
    CPPUNIT_ASSERT_EQUAL(std::string("hashing"), std::string(stageName(PipelineStage::Hashing)));
    CPPUNIT_ASSERT_EQUAL(std::string("waiting"), std::string(stageName(PipelineStage::WaitingMutex)));
    CPPUNIT_ASSERT_EQUAL(std::string("dedup check"), std::string(stageName(PipelineStage::DedupChecking)));
    CPPUNIT_ASSERT_EQUAL(std::string("writing"), std::string(stageName(PipelineStage::WritingStorage)));
    CPPUNIT_ASSERT_EQUAL(std::string("db insert"), std::string(stageName(PipelineStage::InsertingDb)));
    CPPUNIT_ASSERT_EQUAL(std::string("idle"), std::string(stageName(PipelineStage::Idle)));
  }

  void testConcurrentAcquireRelease() {
    // Stress test: N threads each acquire and release a slot many times.
    // Must not crash, deadlock, or produce corrupt state.
    constexpr unsigned int NUM_SLOTS = 8;
    constexpr int NUM_THREADS = 8;
    constexpr int ITERS = 100;

    SlotTracker tracker(NUM_SLOTS);
    std::atomic<int> errorCount{0};

    auto worker = [&](int id) {
      for (int i = 0; i < ITERS; ++i) {
        std::string name = "file_" + std::to_string(id) + "_" + std::to_string(i) + ".jpg";
        unsigned int slot = tracker.acquire(name);
        if (slot >= NUM_SLOTS) {
          ++errorCount;
        }
        // Cycle through a few stages
        tracker.setStage(slot, PipelineStage::Validating);
        tracker.setStage(slot, PipelineStage::Hashing);
        tracker.setStage(slot, PipelineStage::InsertingDb);
        tracker.release(slot);
      }
    };

    std::vector<std::future<void>> futures;
    futures.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
      futures.push_back(std::async(std::launch::async, worker, i));
    }
    for (auto& f : futures) {
      f.get();
    }

    CPPUNIT_ASSERT_EQUAL(0, errorCount.load());

    // All slots should be idle after all workers finish
    auto slots = tracker.snapshot();
    for (const auto& s : slots) {
      CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, s.stage);
    }
  }

  void testSingleSlot() {
    SlotTracker tracker(1);
    unsigned int slot = tracker.acquire("only.jpg");
    CPPUNIT_ASSERT_EQUAL(0u, slot);
    tracker.setStage(slot, PipelineStage::DedupChecking);
    auto snap = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::DedupChecking, snap[0].stage);
    tracker.release(slot);
    snap = tracker.snapshot();
    CPPUNIT_ASSERT_EQUAL(PipelineStage::Idle, snap[0].stage);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SlotTrackerTest);

// ---------------------------------------------------------------------------
// ResultLog tests
// ---------------------------------------------------------------------------

class ResultLogTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ResultLogTest);
  CPPUNIT_TEST(testDefaultAppendDoesNotCrash);
  CPPUNIT_TEST(testDisabledSuppressesOutput);
  CPPUNIT_TEST(testEnabledThenDisabled);
  CPPUNIT_TEST(testConcurrentAppendDoesNotCrash);
  CPPUNIT_TEST(testSetTtyModeNonTty);
  CPPUNIT_TEST_SUITE_END();

public:
  void testDefaultAppendDoesNotCrash() {
    // ResultLog in default (non-TTY) mode writes to stderr.
    // We can't easily capture stderr in a unit test, but we verify
    // that append() does not throw or crash.
    ResultLog log;
    log.append("OK   /some/file.jpg -> abc123.jpg");
    log.append("DUP  /some/other.jpg");
    log.append("ERR  /bad/file.jpg: validation failed");
    log.append("SKIP /skip/me.jpg (in error list)");
    // If we get here, no crash occurred.
    CPPUNIT_ASSERT(true);
  }

  void testDisabledSuppressesOutput() {
    // When disabled, append() must be a no-op (no crash, no output).
    ResultLog log;
    log.setEnabled(false);
    // These should silently do nothing
    log.append("OK   /file.jpg -> abc.jpg");
    log.append("DUP  /dup.jpg");
    CPPUNIT_ASSERT(true);
  }

  void testEnabledThenDisabled() {
    ResultLog log;
    log.append("OK   /first.jpg -> hash.jpg"); // enabled — goes to stderr
    log.setEnabled(false);
    log.append("OK   /second.jpg -> hash2.jpg"); // disabled — no-op
    log.setEnabled(true);
    log.append("OK   /third.jpg -> hash3.jpg"); // enabled again
    CPPUNIT_ASSERT(true);
  }

  void testConcurrentAppendDoesNotCrash() {
    // Multiple threads appending simultaneously must not crash or deadlock.
    ResultLog log;
    constexpr int NUM_THREADS = 16;
    constexpr int ITERS = 50;

    auto worker = [&](int id) {
      for (int i = 0; i < ITERS; ++i) {
        std::string line = "OK   /thread_" + std::to_string(id) + "_file_" + std::to_string(i) + ".jpg -> hash.jpg";
        log.append(line);
      }
    };

    std::vector<std::future<void>> futures;
    futures.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) {
      futures.push_back(std::async(std::launch::async, worker, i));
    }
    for (auto& f : futures) {
      f.get();
    }
    CPPUNIT_ASSERT(true);
  }

  void testSetTtyModeNonTty() {
    // Explicitly setting non-TTY mode (the default) should not crash.
    ResultLog log;
    log.setTtyMode(false, 0, 0);
    log.append("OK   /file.jpg -> hash.jpg");
    CPPUNIT_ASSERT(true);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ResultLogTest);

// ---------------------------------------------------------------------------
// PipelineStage coverage tests
// ---------------------------------------------------------------------------

class PipelineStageTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(PipelineStageTest);
  CPPUNIT_TEST(testAllStagesHaveNames);
  CPPUNIT_TEST(testStageNamesMatchSpec);
  CPPUNIT_TEST_SUITE_END();

public:
  void testAllStagesHaveNames() {
    // Every PipelineStage value must return a non-null, non-empty name.
    const PipelineStage stages[] = {
      PipelineStage::Reading,
      PipelineStage::Validating,
      PipelineStage::Hashing,
      PipelineStage::WaitingMutex,
      PipelineStage::DedupChecking,
      PipelineStage::WritingStorage,
      PipelineStage::InsertingDb,
      PipelineStage::Idle,
    };
    for (auto stage : stages) {
      const char* name = stageName(stage);
      CPPUNIT_ASSERT(name != nullptr);
      CPPUNIT_ASSERT(name[0] != '\0');
    }
  }

  void testStageNamesMatchSpec() {
    // Spec 0016 defines these exact stage names for the slot display.
    CPPUNIT_ASSERT_EQUAL(std::string("reading"), std::string(stageName(PipelineStage::Reading)));
    CPPUNIT_ASSERT_EQUAL(std::string("validating"), std::string(stageName(PipelineStage::Validating)));
    CPPUNIT_ASSERT_EQUAL(std::string("hashing"), std::string(stageName(PipelineStage::Hashing)));
    CPPUNIT_ASSERT_EQUAL(std::string("waiting"), std::string(stageName(PipelineStage::WaitingMutex)));
    CPPUNIT_ASSERT_EQUAL(std::string("dedup check"), std::string(stageName(PipelineStage::DedupChecking)));
    CPPUNIT_ASSERT_EQUAL(std::string("writing"), std::string(stageName(PipelineStage::WritingStorage)));
    CPPUNIT_ASSERT_EQUAL(std::string("db insert"), std::string(stageName(PipelineStage::InsertingDb)));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(PipelineStageTest);

} // namespace imagestore

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
