#include "database.h"

#include <iomanip>
#include <iostream>
#include <ostream>
#include <sys/stat.h>
#include <utility>

namespace db {

namespace {

template <typename T, typename... Args>
std::unique_ptr<T> mk(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

void print_header(const std::vector<std::string>& cols, std::ostream& out) {
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i) out << " | ";
        out << std::setw(8) << std::left << cols[i];
    }
    out << "\n";
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i) out << "-+-";
        out << std::string(8, '-');
    }
    out << "\n";
}

}  // namespace

Database::Database(const std::string& dir) : dir_(dir) {
    ::mkdir(dir.c_str(), 0755);

    pm_   = mk<PageManager>(dir + "/data.db", 64);
    tree_ = mk<BPlusTree>(*pm_);
    wal_  = mk<WAL>(dir + "/data.wal");

    wal_->replay([this](std::int64_t k, std::int64_t v) {
        tree_->insert(k, v);
    });

    table_name_ = "items";
    col_names_ = {"id", "value"};
}

void Database::execute(const std::string& sql, std::ostream& out) {
    Statement st = parse_sql(sql);
    switch (st.kind) {
        case Statement::kCreate:      execute_create(st, out);       break;
        case Statement::kCreateIndex: execute_create_index(st, out); break;
        case Statement::kInsert:      execute_insert(st, out);       break;
        case Statement::kSelect:      execute_select(st, out);       break;
    }
}

void Database::execute_create(const Statement& st, std::ostream& out) {
    if (st.columns.size() != 2) {
        throw SqlError("мини-СУБД поддерживает таблицы только из 2 колонок");
    }
    table_name_ = st.table;
    col_names_ = {st.columns[0].first, st.columns[1].first};
    out << "OK (table '" << table_name_ << "')\n";
}

void Database::execute_create_index(const Statement& st, std::ostream& out) {
    if (st.table != table_name_) throw SqlError("неизвестная таблица: " + st.table);
    if (st.columns.size() != 1) throw SqlError("ожидается одна колонка");
    const std::string& col = st.columns[0].first;
    if (col != col_names_[0] && col != col_names_[1]) {
        throw SqlError("неизвестная колонка: " + col);
    }
    if (indexes_.count(col)) {
        out << "Index уже существует\n";
        return;
    }

    SecondaryIndex idx;
    idx.column = col;
    idx.pm    = mk<PageManager>(dir_ + "/idx_" + st.index_name + ".db", 32);
    idx.tree  = mk<BPlusTree>(*idx.pm);
    idx.wal   = mk<WAL>(dir_ + "/idx_" + st.index_name + ".wal");
    idx.wal->replay([&idx](std::int64_t k, std::int64_t v) {
        idx.tree->insert_dup(k, v);
    });

    int populated = 0;
    tree_->scan([&](std::int64_t row_id, std::int64_t row_value) {
        std::int64_t key = (col == col_names_[0]) ? row_id : row_value;
        idx.wal->log_insert(key, row_id);
        idx.tree->insert_dup(key, row_id);
        ++populated;
    });
    idx.pm->sync();
    idx.wal->truncate();

    indexes_.emplace(col, std::move(idx));
    out << "Index '" << st.index_name << "' on " << col
        << " (" << populated << " rows indexed)\n";
}

void Database::execute_insert(const Statement& st, std::ostream& out) {
    if (st.table != table_name_) throw SqlError("неизвестная таблица: " + st.table);
    if (st.values.size() != 2) throw SqlError("ожидается 2 значения");

    std::int64_t id = st.values[0];
    std::int64_t value = st.values[1];

    wal_->log_insert(id, value);
    tree_->insert(id, value);

    for (auto& kv : indexes_) {
        const std::string& col = kv.first;
        std::int64_t key = (col == col_names_[0]) ? id : value;
        kv.second.wal->log_insert(key, id);
        kv.second.tree->insert_dup(key, id);
    }

    out << "1 row\n";
}

bool Database::matches_where(const Statement& st,
                             std::int64_t id, std::int64_t value) const {
    if (!st.has_where) return true;
    std::int64_t left = (st.where_col == col_names_[0]) ? id : value;
    const auto r = st.where_val;
    const auto& op = st.where_op;
    if (op == "=")  return left == r;
    if (op == "!=") return left != r;
    if (op == "<")  return left <  r;
    if (op == "<=") return left <= r;
    if (op == ">")  return left >  r;
    if (op == ">=") return left >= r;
    throw SqlError("неизвестный оператор: " + op);
}

std::string Database::plan_select(const Statement& st) const {
    if (!st.has_where) return "FullScan(primary)";
    if (st.where_col == col_names_[0] && st.where_op == "=") {
        return "PrimaryLookup(id=" + std::to_string(st.where_val) + ")";
    }
    auto it = indexes_.find(st.where_col);
    if (it != indexes_.end()) {
        if (st.where_op == "=") {
            return "SecondaryLookup(" + it->first + "=" + std::to_string(st.where_val) + ")";
        }
        return "SecondaryScan(" + it->first + " " + st.where_op + " "
             + std::to_string(st.where_val) + ")";
    }
    return "FullScan with filter (" + st.where_col + " " + st.where_op + " "
         + std::to_string(st.where_val) + ")";
}

void Database::run_full_scan(const Statement& st, const std::vector<std::string>& cols,
                             std::ostream& out) {
    int rows = 0;
    tree_->scan([&](std::int64_t id, std::int64_t value) {
        if (!matches_where(st, id, value)) return;
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (i) out << " | ";
            std::int64_t cell = (cols[i] == col_names_[0]) ? id : value;
            out << std::setw(8) << std::left << cell;
        }
        out << "\n";
        ++rows;
    });
    out << "(" << rows << " rows)\n";
}

void Database::run_primary_eq(const Statement& st, const std::vector<std::string>& cols,
                              std::ostream& out) {
    std::int64_t v = 0;
    if (tree_->find(st.where_val, v)) {
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (i) out << " | ";
            std::int64_t cell = (cols[i] == col_names_[0]) ? st.where_val : v;
            out << std::setw(8) << std::left << cell;
        }
        out << "\n(1 row)\n";
    } else {
        out << "(0 rows)\n";
    }
}

void Database::run_secondary(const Statement& st, const std::vector<std::string>& cols,
                             const SecondaryIndex& idx, std::ostream& out) {
    int rows = 0;
    auto emit = [&](std::int64_t row_id) {
        std::int64_t row_value = 0;
        if (!tree_->find(row_id, row_value)) return;
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (i) out << " | ";
            std::int64_t cell = (cols[i] == col_names_[0]) ? row_id : row_value;
            out << std::setw(8) << std::left << cell;
        }
        out << "\n";
        ++rows;
    };

    if (st.where_op == "=") {
        // Вторичный ключ не уникален: на одно значение может приходиться
        // много строк, поэтому берём ВСЕ совпадения, а не первое.
        idx.tree->find_all(st.where_val, emit);
    } else {
        idx.tree->scan([&](std::int64_t key, std::int64_t row_id) {
            const auto r = st.where_val;
            const auto& op = st.where_op;
            bool ok =
                (op == "<"  && key <  r) ||
                (op == "<=" && key <= r) ||
                (op == ">"  && key >  r) ||
                (op == ">=" && key >= r) ||
                (op == "!=" && key != r);
            if (ok) emit(row_id);
        });
    }
    out << "(" << rows << " rows)\n";
}

void Database::execute_select(const Statement& st, std::ostream& out) {
    if (st.table != table_name_) throw SqlError("неизвестная таблица: " + st.table);

    std::string plan = plan_select(st);

    if (st.is_explain) {
        out << "Plan: " << plan << "\n";
        return;
    }

    bool star = (st.select_columns.size() == 1 && st.select_columns[0] == "*");
    std::vector<std::string> cols = star ? col_names_ : st.select_columns;
    print_header(cols, out);

    if (!st.has_where) {
        run_full_scan(st, cols, out);
        return;
    }
    if (st.where_col == col_names_[0] && st.where_op == "=") {
        run_primary_eq(st, cols, out);
        return;
    }
    auto it = indexes_.find(st.where_col);
    if (it != indexes_.end()) {
        run_secondary(st, cols, it->second, out);
        return;
    }
    run_full_scan(st, cols, out);
}

void Database::checkpoint() {
    pm_->sync();
    wal_->truncate();
    for (auto& kv : indexes_) {
        kv.second.pm->sync();
        kv.second.wal->truncate();
    }
}

}  // namespace db
