#ifndef DB_SQL_H
#define DB_SQL_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace db {

class SqlError : public std::runtime_error {
public:
    explicit SqlError(const std::string& m) : std::runtime_error("SQL: " + m) {}
};

// Один statement — упрощённый AST.
// Для учебной цели одна структура с union-полями (kind определяет какие).
struct Statement {
    enum Kind { kCreate, kCreateIndex, kInsert, kSelect };
    Kind kind = kSelect;

    // Префикс EXPLAIN — только для kSelect.
    bool is_explain = false;

    std::string table;

    // CREATE TABLE: список (имя_колонки, тип).
    // CREATE INDEX: только колонки[0].first = имя индексируемой колонки.
    std::vector<std::pair<std::string, std::string>> columns;

    // CREATE INDEX: имя индекса.
    std::string index_name;

    // INSERT: значения по позициям.
    std::vector<std::int64_t> values;

    // SELECT: какие колонки (или "*"), плюс опциональное WHERE.
    std::vector<std::string> select_columns;

    bool has_where = false;
    std::string where_col;
    std::string where_op;        // "=", "<", ">", "<=", ">=", "!="
    std::int64_t where_val = 0;
};

// Парсит ОДИН statement. Завершающую `;` опционально.
// Бросает SqlError при синтаксических ошибках.
Statement parse_sql(const std::string& sql);

}  // namespace db

#endif
