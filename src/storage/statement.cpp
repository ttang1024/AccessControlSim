#include "storage/statement.h"

#include "storage/storage_error.h"

namespace acs::storage {

Statement::Statement(const Database& db, std::string_view sql) : m_db(db.handle()) {
    sqlite3_stmt* raw = nullptr;
    check(sqlite3_prepare_v2(m_db, sql.data(), static_cast<int>(sql.size()), &raw, nullptr));
    m_stmt.reset(raw);
}

Statement& Statement::bind(int index, std::string_view value) {
    // SQLITE_TRANSIENT makes SQLite copy the text, so `value` needn't outlive the call.
    check(sqlite3_bind_text(m_stmt.get(), index, value.data(), static_cast<int>(value.size()),
                            SQLITE_TRANSIENT));
    return *this;
}

Statement& Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(m_stmt.get(), index, value));
    return *this;
}

Statement& Statement::bindNull(int index) {
    check(sqlite3_bind_null(m_stmt.get(), index));
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(m_stmt.get());
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    check(rc);
    return false;
}

void Statement::reset() {
    sqlite3_reset(m_stmt.get());
    sqlite3_clear_bindings(m_stmt.get());
}

std::int64_t Statement::columnInt(int index) const {
    return sqlite3_column_int64(m_stmt.get(), index);
}

std::string Statement::columnText(int index) const {
    const auto* text = sqlite3_column_text(m_stmt.get(), index);
    if (text == nullptr) {
        return {};
    }
    return {reinterpret_cast<const char*>(text),
            static_cast<std::size_t>(sqlite3_column_bytes(m_stmt.get(), index))};
}

bool Statement::columnIsNull(int index) const {
    return sqlite3_column_type(m_stmt.get(), index) == SQLITE_NULL;
}

void Statement::check(int rc) const {
    if (rc != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(m_db));
    }
}

}  // namespace acs::storage
