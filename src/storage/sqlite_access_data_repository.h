#pragma once

#include "storage/access_data_repository.h"
#include "storage/database.h"

namespace acs::storage {

// Lifetime: `db` must outlive the repository.
// Threading: used only by the thread that owns `db`.
class SqliteAccessDataRepository final : public IAccessDataRepository {
public:
    explicit SqliteAccessDataRepository(Database& db);

    [[nodiscard]] engine::AccessSnapshot loadSnapshot() const override;
    void replaceAll(const engine::AccessSnapshot& snapshot) override;

private:
    Database& m_db;
};

}  // namespace acs::storage
