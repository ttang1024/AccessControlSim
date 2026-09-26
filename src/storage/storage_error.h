#pragma once

#include <stdexcept>

namespace acs::storage {

// Thrown by the SQLite wrapper for any failed SQLite call. Callers decide what
// counts as fatal. At startup it ends the program. SqliteEventRepository turns
// it into a `false` return so the writer thread keeps running.
class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace acs::storage
