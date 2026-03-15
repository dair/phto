#include "database/Database.h"

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace db;

// ---------------------------------------------------------------------------
// Helper: create a unique temp db path
// ---------------------------------------------------------------------------
static fs::path tempDbPath(const std::string& suffix = "") {
    return fs::temp_directory_path()
        / ("db_test_" + suffix + std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count())
           + ".db");
}

// ============================================================================
// Construction tests
// ============================================================================
class ConstructionTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(ConstructionTest);
    CPPUNIT_TEST(testCreateNew);
    CPPUNIT_TEST(testOpenExisting);
    CPPUNIT_TEST(testInvalidPath);
    CPPUNIT_TEST_SUITE_END();

    fs::path m_path;

public:
    void setUp() override    { m_path = tempDbPath("ctor"); }
    void tearDown() override { fs::remove(m_path); fs::remove(fs::path(m_path).replace_extension(".db-wal")); fs::remove(fs::path(m_path).replace_extension(".db-shm")); }

    void testCreateNew() {
        CPPUNIT_ASSERT(!fs::exists(m_path));
        Database db(m_path);
        CPPUNIT_ASSERT(fs::exists(m_path));
    }

    void testOpenExisting() {
        { Database db(m_path); } // create
        CPPUNIT_ASSERT_NO_THROW({ Database db(m_path); }); // reopen
    }

    void testInvalidPath() {
        fs::path bad("/nonexistent_dir_xyz/test.db");
        try {
            Database db(bad);
            CPPUNIT_FAIL("Expected DatabaseException");
        } catch (const DatabaseException& e) {
            CPPUNIT_ASSERT(e.code() == DatabaseErrorCode::CreationFailed);
        }
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(ConstructionTest);

// ============================================================================
// File CRUD tests
// ============================================================================
class FileCrudTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(FileCrudTest);
    CPPUNIT_TEST(testAddAndGetFile);
    CPPUNIT_TEST(testAddDuplicate);
    CPPUNIT_TEST(testDeleteFile);
    CPPUNIT_TEST(testDeleteNonExistent);
    CPPUNIT_TEST(testEditFileName);
    CPPUNIT_TEST(testEditNameNonExistent);
    CPPUNIT_TEST(testGetNonExistent);
    CPPUNIT_TEST(testFileExists);
    CPPUNIT_TEST(testFileCount);
    CPPUNIT_TEST(testGetAllFiles);
    CPPUNIT_TEST(testCascadeDeleteOnFile);
    CPPUNIT_TEST_SUITE_END();

    fs::path m_path;
    std::unique_ptr<Database> m_db;

public:
    void setUp() override {
        m_path = tempDbPath("file");
        m_db   = std::make_unique<Database>(m_path);
    }
    void tearDown() override {
        m_db.reset();
        fs::remove(m_path);
        fs::remove(fs::path(m_path).string() + "-wal");
        fs::remove(fs::path(m_path).string() + "-shm");
    }

    void testAddAndGetFile() {
        m_db->addFile("f1", "photo.jpg", 1024, "jpg");
        auto f = m_db->getFile("f1");
        CPPUNIT_ASSERT(f.has_value());
        CPPUNIT_ASSERT_EQUAL(std::string("f1"),        f->id);
        CPPUNIT_ASSERT_EQUAL(std::string("photo.jpg"), f->name);
        CPPUNIT_ASSERT_EQUAL(uint64_t(1024),           f->size);
        CPPUNIT_ASSERT_EQUAL(std::string("jpg"),       f->ext);
    }

    void testAddDuplicate() {
        m_db->addFile("f1", "photo.jpg", 1024, "jpg");
        CPPUNIT_ASSERT_THROW(
            m_db->addFile("f1", "other.jpg", 512, "jpg"),
            DatabaseException);
        try {
            m_db->addFile("f1", "other.jpg", 512, "jpg");
        } catch (const DatabaseException& e) {
            CPPUNIT_ASSERT(e.code() == DatabaseErrorCode::ConstraintViolation);
        }
    }

    void testDeleteFile() {
        m_db->addFile("f1", "photo.jpg", 1024, "jpg");
        m_db->deleteFile("f1");
        CPPUNIT_ASSERT(!m_db->getFile("f1").has_value());
    }

    void testDeleteNonExistent() {
        CPPUNIT_ASSERT_THROW(m_db->deleteFile("nope"), DatabaseException);
        try {
            m_db->deleteFile("nope");
        } catch (const DatabaseException& e) {
            CPPUNIT_ASSERT(e.code() == DatabaseErrorCode::NotFound);
        }
    }

    void testEditFileName() {
        m_db->addFile("f1", "old.jpg", 512, "jpg");
        m_db->editFileName("f1", "new.jpg");
        auto f = m_db->getFile("f1");
        CPPUNIT_ASSERT(f.has_value());
        CPPUNIT_ASSERT_EQUAL(std::string("new.jpg"), f->name);
    }

    void testEditNameNonExistent() {
        CPPUNIT_ASSERT_THROW(m_db->editFileName("nope", "x.jpg"), DatabaseException);
    }

    void testGetNonExistent() {
        CPPUNIT_ASSERT(!m_db->getFile("nope").has_value());
    }

    void testFileExists() {
        CPPUNIT_ASSERT(!m_db->fileExists("f1"));
        m_db->addFile("f1", "a.png", 0, "png");
        CPPUNIT_ASSERT(m_db->fileExists("f1"));
    }

    void testFileCount() {
        CPPUNIT_ASSERT_EQUAL(uint64_t(0), m_db->fileCount());
        m_db->addFile("f1", "a.png", 0, "png");
        m_db->addFile("f2", "b.png", 0, "png");
        CPPUNIT_ASSERT_EQUAL(uint64_t(2), m_db->fileCount());
        m_db->deleteFile("f1");
        CPPUNIT_ASSERT_EQUAL(uint64_t(1), m_db->fileCount());
    }

    void testGetAllFiles() {
        m_db->addFile("f1", "a.png", 10, "png");
        m_db->addFile("f2", "b.png", 20, "png");
        m_db->addFile("f3", "c.png", 30, "png");

        auto all = m_db->getAllFiles();
        CPPUNIT_ASSERT_EQUAL(size_t(3), all.size());

        // Pagination: limit 2
        auto page0 = m_db->getAllFiles(Pagination{0, 2});
        CPPUNIT_ASSERT_EQUAL(size_t(2), page0.size());

        // Pagination: offset past end
        auto page2 = m_db->getAllFiles(Pagination{10, 2});
        CPPUNIT_ASSERT(page2.empty());
    }

    void testCascadeDeleteOnFile() {
        m_db->addFile("f1", "a.png", 0, "png");
        m_db->addTag("landscape");
        m_db->bindTag("f1", "landscape");
        CPPUNIT_ASSERT(!m_db->getTagsForFile("f1").empty());
        m_db->deleteFile("f1");
        // file_tag rows should have been cascade-deleted
        CPPUNIT_ASSERT(m_db->getTagsForFile("f1").empty());
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(FileCrudTest);

// ============================================================================
// Tag CRUD tests
// ============================================================================
class TagCrudTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(TagCrudTest);
    CPPUNIT_TEST(testAddAndCheckTag);
    CPPUNIT_TEST(testAddDuplicate);
    CPPUNIT_TEST(testDeleteTag);
    CPPUNIT_TEST(testDeleteNonExistent);
    CPPUNIT_TEST(testTagCount);
    CPPUNIT_TEST(testGetAllTags);
    CPPUNIT_TEST(testCascadeDeleteOnTag);
    CPPUNIT_TEST_SUITE_END();

    fs::path m_path;
    std::unique_ptr<Database> m_db;

public:
    void setUp() override {
        m_path = tempDbPath("tag");
        m_db   = std::make_unique<Database>(m_path);
    }
    void tearDown() override {
        m_db.reset();
        fs::remove(m_path);
        fs::remove(fs::path(m_path).string() + "-wal");
        fs::remove(fs::path(m_path).string() + "-shm");
    }

    void testAddAndCheckTag() {
        CPPUNIT_ASSERT(!m_db->tagExists("landscape"));
        m_db->addTag("landscape");
        CPPUNIT_ASSERT(m_db->tagExists("landscape"));
    }

    void testAddDuplicate() {
        m_db->addTag("landscape");
        try {
            m_db->addTag("landscape");
            CPPUNIT_FAIL("Expected DatabaseException");
        } catch (const DatabaseException& e) {
            CPPUNIT_ASSERT(e.code() == DatabaseErrorCode::ConstraintViolation);
        }
    }

    void testDeleteTag() {
        m_db->addTag("landscape");
        m_db->deleteTag("landscape");
        CPPUNIT_ASSERT(!m_db->tagExists("landscape"));
    }

    void testDeleteNonExistent() {
        CPPUNIT_ASSERT_THROW(m_db->deleteTag("ghost"), DatabaseException);
    }

    void testTagCount() {
        CPPUNIT_ASSERT_EQUAL(uint64_t(0), m_db->tagCount());
        m_db->addTag("a");
        m_db->addTag("b");
        CPPUNIT_ASSERT_EQUAL(uint64_t(2), m_db->tagCount());
    }

    void testGetAllTags() {
        m_db->addTag("z");
        m_db->addTag("a");
        m_db->addTag("m");
        auto tags = m_db->getAllTags();
        CPPUNIT_ASSERT_EQUAL(size_t(3), tags.size());
        // Should be sorted alphabetically
        CPPUNIT_ASSERT_EQUAL(std::string("a"), tags[0]);
        CPPUNIT_ASSERT_EQUAL(std::string("m"), tags[1]);
        CPPUNIT_ASSERT_EQUAL(std::string("z"), tags[2]);

        // Pagination
        auto page = m_db->getAllTags(Pagination{1, 2});
        CPPUNIT_ASSERT_EQUAL(size_t(2), page.size());
        CPPUNIT_ASSERT_EQUAL(std::string("m"), page[0]);
    }

    void testCascadeDeleteOnTag() {
        m_db->addFile("f1", "a.png", 0, "png");
        m_db->addTag("landscape");
        m_db->bindTag("f1", "landscape");
        m_db->deleteTag("landscape");
        // Binding should have been cascade-deleted
        CPPUNIT_ASSERT(m_db->getTagsForFile("f1").empty());
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(TagCrudTest);

// ============================================================================
// Binding / association tests
// ============================================================================
class BindingTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(BindingTest);
    CPPUNIT_TEST(testBindAndUnbind);
    CPPUNIT_TEST(testBindDuplicate);
    CPPUNIT_TEST(testUnbindNonExistent);
    CPPUNIT_TEST(testBindMissingFile);
    CPPUNIT_TEST(testBindMissingTag);
    CPPUNIT_TEST(testGetTagsForFile);
    CPPUNIT_TEST(testGetTagsForFilePagination);
    CPPUNIT_TEST(testGetFilesByTags_single);
    CPPUNIT_TEST(testGetFilesByTags_multi);
    CPPUNIT_TEST(testGetFilesByTags_empty);
    CPPUNIT_TEST(testGetFilesByTags_pagination);
    CPPUNIT_TEST_SUITE_END();

    fs::path m_path;
    std::unique_ptr<Database> m_db;

public:
    void setUp() override {
        m_path = tempDbPath("bind");
        m_db   = std::make_unique<Database>(m_path);
        m_db->addFile("f1", "a.png", 10, "png");
        m_db->addFile("f2", "b.png", 20, "png");
        m_db->addFile("f3", "c.png", 30, "png");
        m_db->addTag("nature");
        m_db->addTag("urban");
        m_db->addTag("bw");
    }
    void tearDown() override {
        m_db.reset();
        fs::remove(m_path);
        fs::remove(fs::path(m_path).string() + "-wal");
        fs::remove(fs::path(m_path).string() + "-shm");
    }

    void testBindAndUnbind() {
        m_db->bindTag("f1", "nature");
        auto tags = m_db->getTagsForFile("f1");
        CPPUNIT_ASSERT_EQUAL(size_t(1), tags.size());
        CPPUNIT_ASSERT_EQUAL(std::string("nature"), tags[0]);

        m_db->unbindTag("f1", "nature");
        CPPUNIT_ASSERT(m_db->getTagsForFile("f1").empty());
    }

    void testBindDuplicate() {
        m_db->bindTag("f1", "nature");
        try {
            m_db->bindTag("f1", "nature");
            CPPUNIT_FAIL("Expected DatabaseException");
        } catch (const DatabaseException& e) {
            CPPUNIT_ASSERT(e.code() == DatabaseErrorCode::ConstraintViolation);
        }
    }

    void testUnbindNonExistent() {
        CPPUNIT_ASSERT_THROW(m_db->unbindTag("f1", "nature"), DatabaseException);
    }

    void testBindMissingFile() {
        CPPUNIT_ASSERT_THROW(m_db->bindTag("no_such_file", "nature"), DatabaseException);
    }

    void testBindMissingTag() {
        CPPUNIT_ASSERT_THROW(m_db->bindTag("f1", "no_such_tag"), DatabaseException);
    }

    void testGetTagsForFile() {
        m_db->bindTag("f1", "nature");
        m_db->bindTag("f1", "bw");
        auto tags = m_db->getTagsForFile("f1");
        CPPUNIT_ASSERT_EQUAL(size_t(2), tags.size());
        // Sorted
        CPPUNIT_ASSERT_EQUAL(std::string("bw"),     tags[0]);
        CPPUNIT_ASSERT_EQUAL(std::string("nature"), tags[1]);
    }

    void testGetTagsForFilePagination() {
        m_db->bindTag("f1", "nature");
        m_db->bindTag("f1", "bw");
        m_db->bindTag("f1", "urban");

        auto page0 = m_db->getTagsForFile("f1", Pagination{0, 2});
        CPPUNIT_ASSERT_EQUAL(size_t(2), page0.size());

        auto page1 = m_db->getTagsForFile("f1", Pagination{2, 2});
        CPPUNIT_ASSERT_EQUAL(size_t(1), page1.size());

        auto beyond = m_db->getTagsForFile("f1", Pagination{100, 2});
        CPPUNIT_ASSERT(beyond.empty());
    }

    void testGetFilesByTags_single() {
        m_db->bindTag("f1", "nature");
        m_db->bindTag("f2", "nature");
        auto files = m_db->getFilesByTags({"nature"});
        CPPUNIT_ASSERT_EQUAL(size_t(2), files.size());
    }

    void testGetFilesByTags_multi() {
        // f1 has both tags, f2 has only nature
        m_db->bindTag("f1", "nature");
        m_db->bindTag("f1", "bw");
        m_db->bindTag("f2", "nature");

        auto files = m_db->getFilesByTags({"nature", "bw"});
        CPPUNIT_ASSERT_EQUAL(size_t(1), files.size());
        CPPUNIT_ASSERT_EQUAL(std::string("f1"), files[0].id);
    }

    void testGetFilesByTags_empty() {
        auto files = m_db->getFilesByTags({});
        CPPUNIT_ASSERT(files.empty());
    }

    void testGetFilesByTags_pagination() {
        m_db->bindTag("f1", "nature");
        m_db->bindTag("f2", "nature");
        m_db->bindTag("f3", "nature");

        auto page = m_db->getFilesByTags({"nature"}, Pagination{0, 2});
        CPPUNIT_ASSERT_EQUAL(size_t(2), page.size());

        auto page2 = m_db->getFilesByTags({"nature"}, Pagination{2, 2});
        CPPUNIT_ASSERT_EQUAL(size_t(1), page2.size());
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(BindingTest);

// ============================================================================
// Multithreading tests
// ============================================================================
class MultithreadTest : public CppUnit::TestFixture {
    CPPUNIT_TEST_SUITE(MultithreadTest);
    CPPUNIT_TEST(testConcurrentReads);
    CPPUNIT_TEST(testConcurrentWrites);
    CPPUNIT_TEST(testMixedReadWrite);
    CPPUNIT_TEST_SUITE_END();

    fs::path m_path;
    std::unique_ptr<Database> m_db;

    static constexpr int THREAD_COUNT  = 8;
    static constexpr int OPS_PER_THREAD = 50;

public:
    void setUp() override {
        m_path = tempDbPath("mt");
        m_db   = std::make_unique<Database>(m_path);
    }
    void tearDown() override {
        m_db.reset();
        fs::remove(m_path);
        fs::remove(fs::path(m_path).string() + "-wal");
        fs::remove(fs::path(m_path).string() + "-shm");
    }

    // Seed some data for read tests
    void seedData(int fileCount, int tagCount) {
        for (int i = 0; i < fileCount; ++i)
            m_db->addFile("f" + std::to_string(i), "file" + std::to_string(i) + ".png",
                          static_cast<uint64_t>(i * 100), "png");
        for (int i = 0; i < tagCount; ++i)
            m_db->addTag("tag" + std::to_string(i));
    }

    void testConcurrentReads() {
        seedData(20, 5);

        std::vector<std::jthread> threads;
        std::atomic<int> errors{0};

        for (int t = 0; t < THREAD_COUNT; ++t) {
            threads.emplace_back([this, &errors]() {
                try {
                    for (int i = 0; i < OPS_PER_THREAD; ++i) {
                        auto files = m_db->getAllFiles();
                        CPPUNIT_ASSERT_EQUAL(size_t(20), files.size());
                        auto tags = m_db->getAllTags();
                        CPPUNIT_ASSERT_EQUAL(size_t(5), tags.size());
                        m_db->fileCount();
                        m_db->tagCount();
                    }
                } catch (...) {
                    ++errors;
                }
            });
        }
        threads.clear(); // join all

        CPPUNIT_ASSERT_EQUAL(0, errors.load());
    }

    void testConcurrentWrites() {
        std::vector<std::jthread> threads;
        std::atomic<int> errors{0};

        for (int t = 0; t < THREAD_COUNT; ++t) {
            threads.emplace_back([this, t, &errors]() {
                try {
                    for (int i = 0; i < OPS_PER_THREAD; ++i) {
                        std::string id  = "t" + std::to_string(t) + "_f" + std::to_string(i);
                        std::string tag = "t" + std::to_string(t) + "_tag" + std::to_string(i);
                        m_db->addFile(id, "x.png", 0, "png");
                        m_db->addTag(tag);
                        m_db->bindTag(id, tag);
                        m_db->unbindTag(id, tag);
                        m_db->deleteTag(tag);
                        m_db->deleteFile(id);
                    }
                } catch (...) {
                    ++errors;
                }
            });
        }
        threads.clear();

        CPPUNIT_ASSERT_EQUAL(0, errors.load());
        // All data was cleaned up
        CPPUNIT_ASSERT_EQUAL(uint64_t(0), m_db->fileCount());
        CPPUNIT_ASSERT_EQUAL(uint64_t(0), m_db->tagCount());
    }

    void testMixedReadWrite() {
        seedData(10, 3);

        std::vector<std::jthread> threads;
        std::atomic<int> writeErrors{0};
        std::atomic<int> readErrors{0};

        // Writer threads
        for (int t = 0; t < THREAD_COUNT / 2; ++t) {
            threads.emplace_back([this, t, &writeErrors]() {
                try {
                    for (int i = 0; i < OPS_PER_THREAD; ++i) {
                        std::string id = "w" + std::to_string(t) + "_" + std::to_string(i);
                        m_db->addFile(id, "w.png", static_cast<uint64_t>(i), "png");
                        m_db->editFileName(id, "renamed_w.png");
                        m_db->deleteFile(id);
                    }
                } catch (...) {
                    ++writeErrors;
                }
            });
        }

        // Reader threads
        for (int t = 0; t < THREAD_COUNT / 2; ++t) {
            threads.emplace_back([this, &readErrors]() {
                try {
                    for (int i = 0; i < OPS_PER_THREAD; ++i) {
                        m_db->getAllFiles();
                        m_db->getAllTags();
                        m_db->fileCount();
                    }
                } catch (...) {
                    ++readErrors;
                }
            });
        }

        threads.clear();

        CPPUNIT_ASSERT_EQUAL(0, writeErrors.load());
        CPPUNIT_ASSERT_EQUAL(0, readErrors.load());
        // Seed data should be intact
        CPPUNIT_ASSERT_EQUAL(uint64_t(10), m_db->fileCount());
    }
};
CPPUNIT_TEST_SUITE_REGISTRATION(MultithreadTest);

// ============================================================================
// main
// ============================================================================
int main() {
    CppUnit::TextUi::TestRunner runner;
    CppUnit::TestFactoryRegistry& reg = CppUnit::TestFactoryRegistry::getRegistry();
    runner.addTest(reg.makeTest());
    return runner.run() ? 0 : 1;
}
