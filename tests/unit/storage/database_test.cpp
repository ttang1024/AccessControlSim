#include "storage/database.h"

#include <gtest/gtest.h>

#include "storage/statement.h"
#include "storage/storage_error.h"
#include "storage/transaction.h"

namespace acs::storage {
namespace {

class DatabaseTest : public ::testing::Test {
protected:
    DatabaseTest() { m_db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);"); }

    std::int64_t rowCount() {
        Statement count(m_db, "SELECT COUNT(*) FROM t");
        EXPECT_TRUE(count.step());
        return count.columnInt(0);
    }

    Database m_db{":memory:"};
};

TEST_F(DatabaseTest, Statement_BindAndStep_RoundTripsValues) {
    Statement insert(m_db, "INSERT INTO t (id, name) VALUES (?1, ?2)");
    insert.bind(1, std::int64_t{7}).bind(2, "alice");
    EXPECT_FALSE(insert.step());

    Statement select(m_db, "SELECT id, name FROM t");
    ASSERT_TRUE(select.step());
    EXPECT_EQ(select.columnInt(0), 7);
    EXPECT_EQ(select.columnText(1), "alice");
    EXPECT_FALSE(select.step());
}

TEST_F(DatabaseTest, Statement_NullColumn_ReportsNull) {
    Statement insert(m_db, "INSERT INTO t (id, name) VALUES (1, ?1)");
    insert.bindNull(1);
    (void)insert.step();

    Statement select(m_db, "SELECT name FROM t");
    ASSERT_TRUE(select.step());
    EXPECT_TRUE(select.columnIsNull(0));
    EXPECT_EQ(select.columnText(0), "");
}

TEST_F(DatabaseTest, Statement_ResetAndRebind_RunsAgain) {
    Statement insert(m_db, "INSERT INTO t (id) VALUES (?1)");
    for (std::int64_t id = 1; id <= 3; ++id) {
        insert.bind(1, id);
        (void)insert.step();
        insert.reset();
    }
    EXPECT_EQ(rowCount(), 3);
}

TEST_F(DatabaseTest, Statement_InvalidSql_ThrowsStorageError) {
    EXPECT_THROW(Statement(m_db, "SELEKT nonsense"), StorageError);
}

TEST_F(DatabaseTest, Statement_ConstraintViolation_ThrowsStorageError) {
    m_db.execute("INSERT INTO t (id) VALUES (1);");
    Statement duplicate(m_db, "INSERT INTO t (id) VALUES (1)");
    EXPECT_THROW((void)duplicate.step(), StorageError);
}

TEST_F(DatabaseTest, Execute_InvalidSql_ThrowsStorageError) {
    EXPECT_THROW(m_db.execute("DROP TABLE does_not_exist;"), StorageError);
}

TEST_F(DatabaseTest, Transaction_Committed_KeepsChanges) {
    {
        Transaction transaction(m_db);
        m_db.execute("INSERT INTO t (id) VALUES (1);");
        transaction.commit();
    }
    EXPECT_EQ(rowCount(), 1);
}

TEST_F(DatabaseTest, Transaction_NotCommitted_RollsBackOnDestruction) {
    {
        Transaction transaction(m_db);
        m_db.execute("INSERT INTO t (id) VALUES (1);");
    }
    EXPECT_EQ(rowCount(), 0);
}

TEST(DatabaseOpenTest, Constructor_UnopenablePath_ThrowsStorageError) {
    EXPECT_THROW(Database("/nonexistent-dir/sub/acs.db"), StorageError);
}

TEST(DatabaseOpenTest, Constructor_Any_EnablesForeignKeys) {
    Database db(":memory:");
    Statement pragma(db, "PRAGMA foreign_keys");
    ASSERT_TRUE(pragma.step());
    EXPECT_EQ(pragma.columnInt(0), 1);
}

}  // namespace
}  // namespace acs::storage
