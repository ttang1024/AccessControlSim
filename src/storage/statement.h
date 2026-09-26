#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "storage/database.h"

namespace acs::storage {

// A prepared statement (RAII: finalized in the destructor). Every failure
// throws StorageError.
// Note the SQLite convention: bind() indexes start at 1, column indexes at 0.
class Statement {
public:
    Statement(const Database& db, std::string_view sql);

    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, std::int64_t value);
    Statement& bindNull(int index);

    // Runs the statement one row at a time. Returns true when a row is ready,
    // false when finished.
    [[nodiscard]] bool step();

    // Makes the statement ready to run again with new bindings.
    void reset();

    [[nodiscard]] std::int64_t columnInt(int index) const;
    [[nodiscard]] std::string columnText(int index) const;
    [[nodiscard]] bool columnIsNull(int index) const;

private:
    void check(int rc) const;

    struct Finalizer {
        void operator()(sqlite3_stmt* stmt) const { sqlite3_finalize(stmt); }
    };
    sqlite3* m_db;
    std::unique_ptr<sqlite3_stmt, Finalizer> m_stmt;
};

}  // namespace acs::storage
