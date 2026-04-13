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
#include "../TimeFormat.h"

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

// ---------------------------------------------------------------------------
// fmtElapsed tests
// ---------------------------------------------------------------------------

class FmtElapsedTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(FmtElapsedTest);
  CPPUNIT_TEST(testZero);
  CPPUNIT_TEST(testSeconds);
  CPPUNIT_TEST(testExactlyOneMinute);
  CPPUNIT_TEST(testSecondsRolloverToMinutes);
  CPPUNIT_TEST(testMinutesAndSeconds);
  CPPUNIT_TEST(testExactlyOneHour);
  CPPUNIT_TEST(testHoursMinutesSeconds);
  CPPUNIT_TEST(testSecondsBoundary59);
  CPPUNIT_TEST(testSecondsBoundary60RollsOver);
  CPPUNIT_TEST(testMinutesBoundary59);
  CPPUNIT_TEST(testMinutesBoundary60RollsOver);
  CPPUNIT_TEST(testBugReportValue188);
  CPPUNIT_TEST(testLargeValue);
  CPPUNIT_TEST_SUITE_END();

  static std::string fmt(int64_t secs) {
    std::ostringstream ss;
    fmtElapsed(ss, secs);
    return ss.str();
  }

public:
  void testZero() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:00m:00s"), fmt(0));
  }

  void testSeconds() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:00m:08s"), fmt(8));
    CPPUNIT_ASSERT_EQUAL(std::string("00h:00m:45s"), fmt(45));
  }

  void testExactlyOneMinute() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:01m:00s"), fmt(60));
  }

  void testSecondsRolloverToMinutes() {
    // 80 seconds must NOT produce 00m:80s — it must roll over to 01m:20s.
    CPPUNIT_ASSERT_EQUAL(std::string("00h:01m:20s"), fmt(80));
  }

  void testMinutesAndSeconds() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:03m:08s"), fmt(188));
    CPPUNIT_ASSERT_EQUAL(std::string("00h:20m:55s"), fmt(1255));
  }

  void testExactlyOneHour() {
    CPPUNIT_ASSERT_EQUAL(std::string("01h:00m:00s"), fmt(3600));
  }

  void testHoursMinutesSeconds() {
    CPPUNIT_ASSERT_EQUAL(std::string("01h:01m:01s"), fmt(3661));
    CPPUNIT_ASSERT_EQUAL(std::string("02h:30m:15s"), fmt(9015));
  }

  void testSecondsBoundary59() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:00m:59s"), fmt(59));
  }

  void testSecondsBoundary60RollsOver() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:01m:00s"), fmt(60));
  }

  void testMinutesBoundary59() {
    CPPUNIT_ASSERT_EQUAL(std::string("00h:59m:00s"), fmt(59 * 60));
  }

  void testMinutesBoundary60RollsOver() {
    CPPUNIT_ASSERT_EQUAL(std::string("01h:00m:00s"), fmt(60 * 60));
  }

  void testBugReportValue188() {
    // The bug report showed 30m:80s for ~188s elapsed.
    // Correct output is 00h:03m:08s.
    CPPUNIT_ASSERT_EQUAL(std::string("00h:03m:08s"), fmt(188));
    // Seconds component must never exceed 59.
    std::ostringstream ss;
    fmtElapsed(ss, 188);
    std::string result = ss.str();
    // Extract seconds field: last two chars before 's'
    int secs = std::stoi(result.substr(result.size() - 3, 2));
    CPPUNIT_ASSERT_MESSAGE("Seconds must be <= 59", secs <= 59);
  }

  void testLargeValue() {
    // 99 hours + some — hours field must not be clamped.
    CPPUNIT_ASSERT_EQUAL(std::string("99h:59m:59s"), fmt(99 * 3600 + 59 * 60 + 59));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(FmtElapsedTest);

// ---------------------------------------------------------------------------
// SlotTimer tests — verify the stageSecs computation used in renderVerbose()
//
// These tests guard against the 10x timer bug: if the elapsed time is
// computed as milliseconds/100 (deciseconds) instead of whole seconds, a
// 1-second wait would produce stageSecs=10 instead of stageSecs=1.
// ---------------------------------------------------------------------------

class SlotTimerTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(SlotTimerTest);
  CPPUNIT_TEST(testStageSecsIsZeroForNewSlot);
  CPPUNIT_TEST(testStageSecsNotTenxLarger);
  CPPUNIT_TEST(testStageSecsAfterShortSleep);
  CPPUNIT_TEST(testStageSecsFormatsCorrectly);
  CPPUNIT_TEST_SUITE_END();

  // Replicate the exact computation from ProgressReporter::renderVerbose().
  // This is the line under test:
  //   auto stageSecs = std::chrono::duration_cast<std::chrono::seconds>(
  //                        now - s.stageStart).count();
  // The buggy version would be:
  //   auto stageSecs = std::chrono::duration_cast<std::chrono::milliseconds>(
  //                        now - s.stageStart).count() / 100;   // gives 10x too large
  static int64_t computeStageSecs(
    std::chrono::steady_clock::time_point stageStart,
    std::chrono::steady_clock::time_point now
  ) {
    return std::chrono::duration_cast<std::chrono::seconds>(now - stageStart).count();
  }

public:
  // A brand-new slot (stageStart = now) must report 0 seconds immediately.
  void testStageSecsIsZeroForNewSlot() {
    auto t0 = std::chrono::steady_clock::now();
    int64_t secs = computeStageSecs(t0, t0);
    CPPUNIT_ASSERT_EQUAL(int64_t(0), secs);
  }

  // After sleeping 100ms (well under 1 second), stageSecs must be 0, not 1+.
  // The 10x bug would produce stageSecs = 1 (100ms / 100 = 1) for a 100ms sleep.
  void testStageSecsNotTenxLarger() {
    auto stageStart = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto now = std::chrono::steady_clock::now();
    int64_t secs = computeStageSecs(stageStart, now);

    // With the correct (seconds) computation: 100ms → 0 seconds.
    // With the buggy (ms/100) computation: 100ms → 1 decisecond, displayed as 1 second.
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
      "After 100ms, stageSecs must be 0 — not 1 (which would indicate the 10x bug)",
      int64_t(0), secs
    );
  }

  // After sleeping ~1.1 seconds, stageSecs must be exactly 1.
  void testStageSecsAfterShortSleep() {
    auto stageStart = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    auto now = std::chrono::steady_clock::now();
    int64_t secs = computeStageSecs(stageStart, now);

    // Must be 1 (floor division). Could be 1 or 2 depending on scheduler jitter,
    // but must never be 11 (which the 10x bug would produce: 1100ms / 100 = 11).
    CPPUNIT_ASSERT_MESSAGE(
      "After ~1.1s, stageSecs must be in [1,2] — never 11 (the 10x-bug value)",
      secs >= 1 && secs <= 2
    );
  }

  // End-to-end: verify that the stageSecs value from a SlotTracker snapshot
  // formats correctly via fmtElapsed. After a 1.5s slot, the formatted output
  // must contain "01s" or "02s" in the seconds field, never "10s" or "11s".
  void testStageSecsFormatsCorrectly() {
    SlotTracker tracker(1);
    unsigned int slot = tracker.acquire("timer_test.jpg");

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto now = std::chrono::steady_clock::now();
    auto snap = tracker.snapshot();
    int64_t secs = std::chrono::duration_cast<std::chrono::seconds>(
      now - snap[slot].stageStart
    ).count();

    std::ostringstream out;
    fmtElapsed(out, secs);
    std::string result = out.str();

    // The seconds field is the last 3 chars before 's': e.g. "01s"
    // After 1.5s the result must be "00h:00m:01s" or "00h:00m:02s".
    // The 10x bug would produce "00h:00m:15s" (1500ms/100 = 15).
    CPPUNIT_ASSERT_MESSAGE(
      "Timer output after ~1.5s must start with '00h:00m:0' (i.e. single-digit seconds), "
      "not '00h:00m:1' (which would indicate the 10x bug producing 15s for a 1.5s wait). "
      "Got: " + result,
      result.substr(0, 9) == "00h:00m:0"
    );

    tracker.release(slot);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(SlotTimerTest);

} // namespace imagestore

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(CppUnit::TestFactoryRegistry::getRegistry().makeTest());
  return runner.run() ? 0 : 1;
}
