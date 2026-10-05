#include "ishtariaadmin/Db.h"

#include <memory>

namespace ishtariaadmin {

namespace {
struct ResultDeleter {
    void operator()(PGresult *r) const { PQclear(r); }
};
} // namespace

Db::Db(const std::string &url) : conn_(PQconnectdb(url.c_str())) {
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string message = PQerrorMessage(conn_);
        PQfinish(conn_);
        throw DbError("cannot connect to PostgreSQL: " + message);
    }
}

Db::~Db() { PQfinish(conn_); }

Rows Db::exec(const std::string &sql, const std::vector<Param> &params) {
    std::vector<const char *> values;
    values.reserve(params.size());
    for (const auto &p : params) {
        values.push_back(p ? p->c_str() : nullptr);
    }
    std::unique_ptr<PGresult, ResultDeleter> result(
        PQexecParams(conn_, sql.c_str(), static_cast<int>(values.size()), nullptr,
                     values.data(), nullptr, nullptr, 0));
    const auto status = PQresultStatus(result.get());
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        throw DbError(PQresultErrorMessage(result.get()));
    }
    const char *count = PQcmdTuples(result.get());
    affected_ = (count != nullptr && *count != '\0') ? std::atol(count) : 0;
    Rows rows;
    const int n = PQntuples(result.get());
    const int cols = PQnfields(result.get());
    for (int i = 0; i < n; ++i) {
        Row row;
        for (int j = 0; j < cols; ++j) {
            row.emplace_back(PQgetisnull(result.get(), i, j) ? "" : PQgetvalue(result.get(), i, j));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

Transaction::Transaction(Db &db) : db_(db) { db_.exec("BEGIN"); }

Transaction::~Transaction() {
    if (!done_) {
        try {
            db_.exec("ROLLBACK");
        } catch (...) {
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT");
    done_ = true;
}

} // namespace ishtariaadmin
