#pragma once

#include <libpq-fe.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ishtariaadmin {

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Param = std::optional<std::string>;
using Row = std::vector<std::string>;
using Rows = std::vector<Row>;

// Thin RAII wrapper over libpq. All values are passed as parameters, never
// interpolated into SQL. NULL columns are returned as empty strings.
class Db {
public:
    explicit Db(const std::string &url);
    ~Db();
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;

    Rows exec(const std::string &sql, const std::vector<Param> &params = {});
    // Number of rows affected by the last INSERT/UPDATE/DELETE.
    long affected() const { return affected_; }

private:
    PGconn *conn_;
    long affected_ = 0;
};

// BEGIN ... COMMIT scope; rolls back unless commit() was called.
class Transaction {
public:
    explicit Transaction(Db &db);
    ~Transaction();
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    void commit();

private:
    Db &db_;
    bool done_ = false;
};

} // namespace ishtariaadmin
