#include "storage/transaction.h"

namespace acs::storage {

Transaction::Transaction(Database& db) : m_db(db) { m_db.execute("BEGIN IMMEDIATE;"); }

Transaction::~Transaction() {
    if (!m_committed) {
        // Destructors must not throw, and a failed rollback leaves nothing to fix up.
        sqlite3_exec(m_db.handle(), "ROLLBACK;", nullptr, nullptr, nullptr);
    }
}

void Transaction::commit() {
    m_db.execute("COMMIT;");
    m_committed = true;
}

}  // namespace acs::storage
