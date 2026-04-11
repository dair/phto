#include <coro/BlockOn.h>
#include <coro/Task.h>
#include <coro/ThreadPool.h>
#include <coro/WhenAll.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace coro;
using namespace std::chrono_literals;

// ============================================================================
// ThreadPool + blockOn basic tests
// ============================================================================
class ThreadPoolBlockOnTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ThreadPoolBlockOnTest);
  CPPUNIT_TEST(testSimpleTaskReturnsValue);
  CPPUNIT_TEST(testVoidTaskCompletes);
  CPPUNIT_TEST(testTaskRunsOnWorkerThread);
  CPPUNIT_TEST_SUITE_END();

  ThreadPool* m_pool{nullptr};

public:
  void setUp() override {
    m_pool = new ThreadPool(2);
  }

  void tearDown() override {
    delete m_pool;
    m_pool = nullptr;
  }

  void testSimpleTaskReturnsValue() {
    auto task = [](ThreadPool& pool) -> Task<int> {
      co_await pool.schedule();
      co_return 42;
    }(*m_pool);

    int result = blockOn(*m_pool, std::move(task));
    CPPUNIT_ASSERT_EQUAL(42, result);
  }

  void testVoidTaskCompletes() {
    bool ran = false;
    auto task = [](ThreadPool& pool, bool& flag) -> Task<void> {
      co_await pool.schedule();
      flag = true;
    }(*m_pool, ran);

    CPPUNIT_ASSERT_NO_THROW(blockOn(*m_pool, std::move(task)));
    CPPUNIT_ASSERT(ran);
  }

  void testTaskRunsOnWorkerThread() {
    std::thread::id callerThreadId = std::this_thread::get_id();
    std::thread::id taskThreadId{};

    auto task = [](ThreadPool& pool, std::thread::id& tid) -> Task<void> {
      co_await pool.schedule();
      tid = std::this_thread::get_id();
    }(*m_pool, taskThreadId);

    blockOn(*m_pool, std::move(task));
    // Task must run on a different thread than the caller
    CPPUNIT_ASSERT(taskThreadId != callerThreadId);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ThreadPoolBlockOnTest);

// ============================================================================
// whenAll<int> tests
// ============================================================================
class WhenAllIntTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(WhenAllIntTest);
  CPPUNIT_TEST(testThreeTasksAllComplete);
  CPPUNIT_TEST(testResultsContainAllValues);
  CPPUNIT_TEST(testEmptyVectorReturnsEmpty);
  CPPUNIT_TEST(testExceptionPropagatesToBlockOn);
  CPPUNIT_TEST_SUITE_END();

  ThreadPool* m_pool{nullptr};

public:
  void setUp() override {
    m_pool = new ThreadPool(4);
  }

  void tearDown() override {
    delete m_pool;
    m_pool = nullptr;
  }

  void testThreeTasksAllComplete() {
    auto makeTask = [](ThreadPool& pool, int val) -> Task<int> {
      co_await pool.schedule();
      co_return val;
    };

    auto outer = [](ThreadPool& pool, auto makeTask) -> Task<std::vector<int>> {
      co_await pool.schedule();
      std::vector<Task<int>> tasks;
      tasks.push_back(makeTask(pool, 1));
      tasks.push_back(makeTask(pool, 2));
      tasks.push_back(makeTask(pool, 3));
      co_return co_await whenAll(std::move(tasks));
    }(*m_pool, makeTask);

    auto results = blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT_EQUAL(size_t(3), results.size());
  }

  void testResultsContainAllValues() {
    auto makeTask = [](ThreadPool& pool, int val) -> Task<int> {
      co_await pool.schedule();
      co_return val;
    };

    auto outer = [](ThreadPool& pool, auto makeTask) -> Task<std::vector<int>> {
      co_await pool.schedule();
      std::vector<Task<int>> tasks;
      tasks.push_back(makeTask(pool, 10));
      tasks.push_back(makeTask(pool, 20));
      tasks.push_back(makeTask(pool, 30));
      co_return co_await whenAll(std::move(tasks));
    }(*m_pool, makeTask);

    auto results = blockOn(*m_pool, std::move(outer));
    // Order is preserved by index
    int sum = 0;
    for (int v : results) {
      sum += v;
    }
    CPPUNIT_ASSERT_EQUAL(60, sum);
  }

  void testEmptyVectorReturnsEmpty() {
    auto outer = [](ThreadPool& pool) -> Task<std::vector<int>> {
      co_await pool.schedule();
      std::vector<Task<int>> tasks;
      co_return co_await whenAll(std::move(tasks));
    }(*m_pool);

    auto results = blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT(results.empty());
  }

  void testExceptionPropagatesToBlockOn() {
    auto throwingTask = [](ThreadPool& pool) -> Task<int> {
      co_await pool.schedule();
      throw std::runtime_error("task failed");
      co_return 0;
    };

    auto outer = [](ThreadPool& pool, auto throwingTask) -> Task<std::vector<int>> {
      co_await pool.schedule();
      std::vector<Task<int>> tasks;
      tasks.push_back(throwingTask(pool));
      co_return co_await whenAll(std::move(tasks));
    }(*m_pool, throwingTask);

    CPPUNIT_ASSERT_THROW(blockOn(*m_pool, std::move(outer)), std::runtime_error);
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(WhenAllIntTest);

// ============================================================================
// whenAll<void> tests
// ============================================================================
class WhenAllVoidTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(WhenAllVoidTest);
  CPPUNIT_TEST(testThreeVoidTasksAllIncrement);
  CPPUNIT_TEST(testEmptyVoidVectorCompletes);
  CPPUNIT_TEST_SUITE_END();

  ThreadPool* m_pool{nullptr};

public:
  void setUp() override {
    m_pool = new ThreadPool(4);
  }

  void tearDown() override {
    delete m_pool;
    m_pool = nullptr;
  }

  void testThreeVoidTasksAllIncrement() {
    std::atomic<int> counter{0};

    auto makeTask = [](ThreadPool& pool, std::atomic<int>& ctr) -> Task<void> {
      co_await pool.schedule();
      ctr.fetch_add(1, std::memory_order_relaxed);
    };

    auto outer = [](ThreadPool& pool, std::atomic<int>& ctr, auto makeTask) -> Task<void> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      tasks.push_back(makeTask(pool, ctr));
      tasks.push_back(makeTask(pool, ctr));
      tasks.push_back(makeTask(pool, ctr));
      co_await whenAll(std::move(tasks));
    }(*m_pool, counter, makeTask);

    blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT_EQUAL(3, counter.load(std::memory_order_acquire));
  }

  void testEmptyVoidVectorCompletes() {
    auto outer = [](ThreadPool& pool) -> Task<void> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      co_await whenAll(std::move(tasks));
    }(*m_pool);

    CPPUNIT_ASSERT_NO_THROW(blockOn(*m_pool, std::move(outer)));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(WhenAllVoidTest);

// ============================================================================
// whenAllSettled tests
// ============================================================================
class WhenAllSettledTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(WhenAllSettledTest);
  CPPUNIT_TEST(testAllTasksCompleteEvenIfOneThrows);
  CPPUNIT_TEST(testSuccessfulTaskHasNullptr);
  CPPUNIT_TEST(testFailingTaskHasExceptionPtr);
  CPPUNIT_TEST(testEmptySettledReturnsEmpty);
  CPPUNIT_TEST_SUITE_END();

  ThreadPool* m_pool{nullptr};

public:
  void setUp() override {
    m_pool = new ThreadPool(4);
  }

  void tearDown() override {
    delete m_pool;
    m_pool = nullptr;
  }

  void testAllTasksCompleteEvenIfOneThrows() {
    std::atomic<int> counter{0};

    auto goodTask = [](ThreadPool& pool, std::atomic<int>& ctr) -> Task<void> {
      co_await pool.schedule();
      ctr.fetch_add(1, std::memory_order_relaxed);
    };

    auto badTask = [](ThreadPool& pool, std::atomic<int>& ctr) -> Task<void> {
      co_await pool.schedule();
      ctr.fetch_add(1, std::memory_order_relaxed);
      throw std::runtime_error("intentional failure");
    };

    auto outer = [](ThreadPool& pool, std::atomic<int>& ctr, auto goodTask,
                    auto badTask) -> Task<std::vector<std::exception_ptr>> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      tasks.push_back(goodTask(pool, ctr));
      tasks.push_back(badTask(pool, ctr));
      tasks.push_back(goodTask(pool, ctr));
      co_return co_await whenAllSettled(std::move(tasks));
    }(*m_pool, counter, goodTask, badTask);

    auto results = blockOn(*m_pool, std::move(outer));
    // All 3 tasks ran (2 good + 1 bad)
    CPPUNIT_ASSERT_EQUAL(3, counter.load(std::memory_order_acquire));
    CPPUNIT_ASSERT_EQUAL(size_t(3), results.size());
  }

  void testSuccessfulTaskHasNullptr() {
    auto goodTask = [](ThreadPool& pool) -> Task<void> {
      co_await pool.schedule();
    };

    auto outer = [](ThreadPool& pool, auto goodTask) -> Task<std::vector<std::exception_ptr>> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      tasks.push_back(goodTask(pool));
      co_return co_await whenAllSettled(std::move(tasks));
    }(*m_pool, goodTask);

    auto results = blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT_EQUAL(size_t(1), results.size());
    CPPUNIT_ASSERT(results[0] == nullptr);
  }

  void testFailingTaskHasExceptionPtr() {
    auto badTask = [](ThreadPool& pool) -> Task<void> {
      co_await pool.schedule();
      throw std::runtime_error("whoops");
    };

    auto outer = [](ThreadPool& pool, auto badTask) -> Task<std::vector<std::exception_ptr>> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      tasks.push_back(badTask(pool));
      co_return co_await whenAllSettled(std::move(tasks));
    }(*m_pool, badTask);

    auto results = blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT_EQUAL(size_t(1), results.size());
    CPPUNIT_ASSERT(results[0] != nullptr);
    // Verify the captured exception is what was thrown
    CPPUNIT_ASSERT_THROW(std::rethrow_exception(results[0]), std::runtime_error);
  }

  void testEmptySettledReturnsEmpty() {
    auto outer = [](ThreadPool& pool) -> Task<std::vector<std::exception_ptr>> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      co_return co_await whenAllSettled(std::move(tasks));
    }(*m_pool);

    auto results = blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT(results.empty());
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(WhenAllSettledTest);

// ============================================================================
// Concurrency stress test
// ============================================================================
class ConcurrencyTest : public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(ConcurrencyTest);
  CPPUNIT_TEST(testTenTasksFourThreadsAllComplete);
  CPPUNIT_TEST_SUITE_END();

  ThreadPool* m_pool{nullptr};

public:
  void setUp() override {
    m_pool = new ThreadPool(4);
  }

  void tearDown() override {
    delete m_pool;
    m_pool = nullptr;
  }

  void testTenTasksFourThreadsAllComplete() {
    std::atomic<int> counter{0};

    auto makeTask = [](ThreadPool& pool, std::atomic<int>& ctr) -> Task<void> {
      co_await pool.schedule();
      std::this_thread::sleep_for(1ms);
      ctr.fetch_add(1, std::memory_order_relaxed);
    };

    auto outer = [](ThreadPool& pool, std::atomic<int>& ctr, auto makeTask) -> Task<void> {
      co_await pool.schedule();
      std::vector<Task<void>> tasks;
      tasks.reserve(10);
      for (int i = 0; i < 10; ++i) {
        tasks.push_back(makeTask(pool, ctr));
      }
      co_await whenAll(std::move(tasks));
    }(*m_pool, counter, makeTask);

    blockOn(*m_pool, std::move(outer));
    CPPUNIT_ASSERT_EQUAL(10, counter.load(std::memory_order_acquire));
  }
};

CPPUNIT_TEST_SUITE_REGISTRATION(ConcurrencyTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry &reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
