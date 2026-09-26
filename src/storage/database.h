#pragma once

#include <sqlite3.h>

#include <memory>
#include <string>

namespace acs::storage {

// Owns one SQLite connection (RAII: closed in the destructor).
// Threading: a connection is used by one thread at a time. Each thread that
// needs the database opens its own Database. The server has one for startup
// loading and one for the audit writer thread.
class Database {
public:
    // Opens or creates the database at `path`. Use ":memory:" for a private
    // in-memory database. Turns on foreign keys, WAL journaling (for files)
    // and a busy timeout. Throws StorageError on failure.
    explicit Database(const std::string& path);

    // Runs one or more SQL statements that return no rows we need.
    // Throws StorageError on failure.
    void execute(const std::string& sql);

    [[nodiscard]] sqlite3* handle() const { return m_db.get(); }

private:
    struct Closer {
        void operator()(sqlite3* db) const { sqlite3_close(db); }
    };
    std::unique_ptr<sqlite3, Closer> m_db;
};

}  // namespace acs::storage
