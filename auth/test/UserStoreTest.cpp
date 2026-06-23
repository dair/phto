#include <auth/PasswordHash.h>
#include <auth/UserStore.h>
#include <auth/types/AuthError.h>
#include <auth/types/User.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <database/Database.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using auth::AuthErrorCode;
using auth::AuthException;
using auth::hashPassword;
using auth::PasswordRecord;
using auth::User;
using auth::UserStore;

// ============================================================================
// UserStore tests
// ============================================================================
class UserStoreTest: public CppUnit::TestFixture {
  CPPUNIT_TEST_SUITE(UserStoreTest);
  CPPUNIT_TEST(testCreateAndGet);
  CPPUNIT_TEST(testGetPasswordRoundTrip);
  CPPUNIT_TEST(testDuplicateThrows);
  CPPUNIT_TEST(testRemove);
  CPPUNIT_TEST(testRemoveMissingThrows);
  CPPUNIT_TEST(testSetEnabled);
  CPPUNIT_TEST(testSetAdmin);
  CPPUNIT_TEST(testSetPassword);
  CPPUNIT_TEST(testSetEnabledMissingThrows);
  CPPUNIT_TEST(testSetAdminMissingThrows);
  CPPUNIT_TEST(testSetPasswordMissingThrows);
  CPPUNIT_TEST(testListOrdering);
  CPPUNIT_TEST(testListPagination);
  CPPUNIT_TEST(testListOffsetBeyondEnd);
  CPPUNIT_TEST(testCount);
  CPPUNIT_TEST(testConcurrency);
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp() override {
    m_dbPath = std::filesystem::temp_directory_path() / "auth_userstore_test.db";
    std::filesystem::remove(m_dbPath);
    m_store = std::make_unique<UserStore>(m_dbPath);
  }

  void tearDown() override {
    m_store.reset();
    std::filesystem::remove(m_dbPath);
  }

  // --- helpers ---

  static PasswordRecord makePw(const std::string& password) {
    return hashPassword(password, 1000);
  }

  void addUser(const std::string& login, const std::string& fullName, bool isAdmin = false) {
    m_store->create(login, fullName, makePw("pw-" + login), isAdmin);
  }

  // --- tests ---

  void testCreateAndGet() {
    PasswordRecord pw = makePw("secret");
    m_store->create("alice", "Alice Liddell", pw, true);

    auto u = m_store->get("alice");
    CPPUNIT_ASSERT(u.has_value());
    CPPUNIT_ASSERT_EQUAL(std::string("alice"), u->login);
    CPPUNIT_ASSERT_EQUAL(std::string("Alice Liddell"), u->fullName);
    CPPUNIT_ASSERT(u->isAdmin);
    CPPUNIT_ASSERT(u->enabled);
    CPPUNIT_ASSERT(u->createdAt > 0);
    CPPUNIT_ASSERT_EQUAL(u->createdAt, u->updatedAt);

    auto absent = m_store->get("nobody");
    CPPUNIT_ASSERT(!absent.has_value());
  }

  void testGetPasswordRoundTrip() {
    PasswordRecord pw = makePw("hunter2");
    m_store->create("bob", "Bob Builder", pw, false);

    auto stored = m_store->getPassword("bob");
    CPPUNIT_ASSERT(stored.has_value());
    CPPUNIT_ASSERT_EQUAL(pw.algo, stored->algo);
    CPPUNIT_ASSERT_EQUAL(pw.iterations, stored->iterations);
    CPPUNIT_ASSERT(pw.salt == stored->salt);
    CPPUNIT_ASSERT(pw.hash == stored->hash);

    // Verify the stored record produces a correct password check.
    CPPUNIT_ASSERT(auth::verifyPassword("hunter2", *stored));
    CPPUNIT_ASSERT(!auth::verifyPassword("wrong", *stored));

    auto absent = m_store->getPassword("nobody");
    CPPUNIT_ASSERT(!absent.has_value());
  }

  void testDuplicateThrows() {
    addUser("carol", "Carol Danvers");
    bool threw = false;
    try {
      addUser("carol", "Carol Clone");
    } catch (const AuthException& e) {
      CPPUNIT_ASSERT(e.code() == AuthErrorCode::Duplicate);
      threw = true;
    }
    CPPUNIT_ASSERT(threw);
  }

  void testRemove() {
    addUser("dave", "Dave Grohl");
    CPPUNIT_ASSERT(m_store->get("dave").has_value());
    m_store->remove("dave");
    CPPUNIT_ASSERT(!m_store->get("dave").has_value());
  }

  void testRemoveMissingThrows() {
    bool threw = false;
    try {
      m_store->remove("ghost");
    } catch (const AuthException& e) {
      CPPUNIT_ASSERT(e.code() == AuthErrorCode::NotFound);
      threw = true;
    }
    CPPUNIT_ASSERT(threw);
  }

  void testSetEnabled() {
    addUser("eve", "Eve Online");
    CPPUNIT_ASSERT(m_store->get("eve")->enabled);
    m_store->setEnabled("eve", false);
    CPPUNIT_ASSERT(!m_store->get("eve")->enabled);
    m_store->setEnabled("eve", true);
    CPPUNIT_ASSERT(m_store->get("eve")->enabled);
  }

  void testSetAdmin() {
    addUser("frank", "Frank Castle", false);
    CPPUNIT_ASSERT(!m_store->get("frank")->isAdmin);
    m_store->setAdmin("frank", true);
    CPPUNIT_ASSERT(m_store->get("frank")->isAdmin);
    m_store->setAdmin("frank", false);
    CPPUNIT_ASSERT(!m_store->get("frank")->isAdmin);
  }

  void testSetPassword() {
    addUser("grace", "Grace Hopper");
    PasswordRecord newPw = makePw("newpassword");
    m_store->setPassword("grace", newPw);
    auto stored = m_store->getPassword("grace");
    CPPUNIT_ASSERT(stored.has_value());
    CPPUNIT_ASSERT(auth::verifyPassword("newpassword", *stored));
    CPPUNIT_ASSERT(!auth::verifyPassword("pw-grace", *stored));
  }

  void testSetEnabledMissingThrows() {
    bool threw = false;
    try {
      m_store->setEnabled("nobody", false);
    } catch (const AuthException& e) {
      CPPUNIT_ASSERT(e.code() == AuthErrorCode::NotFound);
      threw = true;
    }
    CPPUNIT_ASSERT(threw);
  }

  void testSetAdminMissingThrows() {
    bool threw = false;
    try {
      m_store->setAdmin("nobody", true);
    } catch (const AuthException& e) {
      CPPUNIT_ASSERT(e.code() == AuthErrorCode::NotFound);
      threw = true;
    }
    CPPUNIT_ASSERT(threw);
  }

  void testSetPasswordMissingThrows() {
    bool threw = false;
    try {
      m_store->setPassword("nobody", makePw("x"));
    } catch (const AuthException& e) {
      CPPUNIT_ASSERT(e.code() == AuthErrorCode::NotFound);
      threw = true;
    }
    CPPUNIT_ASSERT(threw);
  }

  void testListOrdering() {
    addUser("zara", "Zara Z");
    addUser("alice", "Alice A");
    addUser("mike", "Mike M");

    auto users = m_store->list();
    CPPUNIT_ASSERT_EQUAL(size_t(3), users.size());
    CPPUNIT_ASSERT_EQUAL(std::string("alice"), users[0].login);
    CPPUNIT_ASSERT_EQUAL(std::string("mike"), users[1].login);
    CPPUNIT_ASSERT_EQUAL(std::string("zara"), users[2].login);
  }

  void testListPagination() {
    addUser("aaa", "Aaa");
    addUser("bbb", "Bbb");
    addUser("ccc", "Ccc");
    addUser("ddd", "Ddd");

    db::Pagination page{0, 2};
    auto p1 = m_store->list(page);
    CPPUNIT_ASSERT_EQUAL(size_t(2), p1.size());
    CPPUNIT_ASSERT_EQUAL(std::string("aaa"), p1[0].login);
    CPPUNIT_ASSERT_EQUAL(std::string("bbb"), p1[1].login);

    db::Pagination page2{2, 2};
    auto p2 = m_store->list(page2);
    CPPUNIT_ASSERT_EQUAL(size_t(2), p2.size());
    CPPUNIT_ASSERT_EQUAL(std::string("ccc"), p2[0].login);
    CPPUNIT_ASSERT_EQUAL(std::string("ddd"), p2[1].login);
  }

  void testListOffsetBeyondEnd() {
    addUser("only", "Only One");
    db::Pagination page{100, 10};
    auto result = m_store->list(page);
    CPPUNIT_ASSERT(result.empty());
  }

  void testCount() {
    CPPUNIT_ASSERT_EQUAL(uint64_t(0), m_store->count());
    addUser("u1", "U1");
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), m_store->count());
    addUser("u2", "U2");
    CPPUNIT_ASSERT_EQUAL(uint64_t(2), m_store->count());
    m_store->remove("u1");
    CPPUNIT_ASSERT_EQUAL(uint64_t(1), m_store->count());
  }

  void testConcurrency() {
    // Seed some users to read.
    for (int i = 0; i < 10; ++i) {
      addUser("user" + std::to_string(i), "User " + std::to_string(i));
    }

    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 50;
    std::atomic<int> errors{0};

    auto worker = [&](int tid) {
      try {
        for (int op = 0; op < OPS_PER_THREAD; ++op) {
          // Alternate between reads and writes.
          if (op % 3 == 0) {
            // Read: list or count
            if (op % 6 == 0) {
              m_store->list();
            } else {
              m_store->count();
            }
          } else if (op % 3 == 1) {
            // Read: get
            auto u = m_store->get("user" + std::to_string(op % 10));
            (void)u;
          } else {
            // Write: toggle enabled
            const std::string login = "user" + std::to_string((tid + op) % 10);
            try {
              m_store->setEnabled(login, (op % 2) == 0);
            } catch (const AuthException&) {
              // NotFound is acceptable under concurrent deletes (none here, but guard)
            }
          }
        }
      } catch (...) {
        ++errors;
      }
    };

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);
    for (int t = 0; t < NUM_THREADS; ++t) {
      threads.emplace_back(worker, t);
    }
    for (auto& th : threads) {
      th.join();
    }

    CPPUNIT_ASSERT_EQUAL(0, errors.load());
    // Verify data integrity: all 10 original users still present.
    CPPUNIT_ASSERT_EQUAL(uint64_t(10), m_store->count());
  }

private:
  std::filesystem::path m_dbPath;
  std::unique_ptr<UserStore> m_store;
};

CPPUNIT_TEST_SUITE_REGISTRATION(UserStoreTest);

// ============================================================================
// main
// ============================================================================
int main() {
  CppUnit::TextUi::TestRunner runner;
  CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
  runner.addTest(reg.makeTest());
  return runner.run() ? 0 : 1;
}
