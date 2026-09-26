#pragma once

#include "storage/database.h"

namespace acs::storage {

// A scoped write transaction. It rolls back unless commit() is called, so a
// thrown exception can never leave half-written data behind.
// Uses BEGIN IMMEDIATE, which takes the write lock up front. A plain BEGIN
// takes it at the first write, which can fail with SQLITE_BUSY halfway
// through when another connection is also writing.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database& m_db;
    bool m_committed = false;
};

}  // namespace acs::storage
