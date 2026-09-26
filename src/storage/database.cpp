#include "storage/database.h"

#include "storage/storage_error.h"

namespace acs::storage {

namespace {

constexpr int kBusyTimeoutMs = 5000;

}  // namespace

Database::Database(const std::string& path) {
    sqlite3* raw = nullptr;
    const int rc =
        sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    // SQLite may allocate a handle even when opening fails, so take ownership first.
    m_db.reset(raw);
    if (rc != SQLITE_OK) {
        throw StorageError("cannot open database '" + path +
                           "': " + (raw != nullptr ? sqlite3_errmsg(raw) : sqlite3_errstr(rc)));
    }

    sqlite3_busy_timeout(m_db.get(), kBusyTimeoutMs);
    // SQLite leaves foreign keys off unless each connection asks for them.
    execute("PRAGMA foreign_keys = ON;");
    // WAL lets readers work while the audit writer is writing. In-memory
    // databases ignore this.
    execute("PRAGMA journal_mode = WAL;");
}

void Database::execute(const std::string& sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(m_db.get(), sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error != nullptr ? error : sqlite3_errstr(rc);
        sqlite3_free(error);
        throw StorageError(message);
    }
}

}  // namespace acs::storage
