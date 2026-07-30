#include "database.h"

#include <fstream>
#include <iomanip>
#include <sstream>
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

// Согласование числительного: 1 строка, 2-4 строки, 5+ строк.
void print_row_count(int rows, std::ostream& out) {
    int last_two = rows % 100;
    int last = rows % 10;
    const char* word = "строк";
    if (last == 1 && last_two != 11) word = "строка";
    else if (last >= 2 && last <= 4 && (last_two < 12 || last_two > 14)) word = "строки";
    out << "(" << rows << " " << word << ")\n";
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

    // Значения по умолчанию — на случай пустой базы без каталога.
    table_name_ = "items";
    col_names_ = {"id", "value"};

    load_catalog();
}

// Каталог — три строки: имя таблицы, имена колонок, дальше по строке на индекс.
//   items
//   id value
//   idx_val value
void Database::load_catalog() {
    std::ifstream in(dir_ + "/catalog.txt");
    if (!in.is_open()) return;

    std::string table;
    if (!std::getline(in, table) || table.empty()) return;
    table_name_ = table;

    std::string cols;
    if (std::getline(in, cols)) {
        std::istringstream cs(cols);
        std::vector<std::string> names;
        std::string n;
        while (cs >> n) names.push_back(n);
        if (names.size() == 2) col_names_ = names;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string index_name, column;
        if (!(ls >> index_name >> column)) continue;
        open_index(index_name, column);
    }
}

void Database::save_catalog() {
    std::ofstream out(dir_ + "/catalog.txt");
    if (!out.is_open()) return;
    out << table_name_ << "\n";
    out << col_names_[0] << " " << col_names_[1] << "\n";
    for (const auto& kv : index_names_) {
        out << kv.second << " " << kv.first << "\n";
    }
}

// Открыть (или создать) файлы индекса и восстановить его из журнала.
void Database::open_index(const std::string& index_name, const std::string& column) {
    if (indexes_.count(column)) return;

    SecondaryIndex idx;
    idx.column = column;
    idx.pm    = mk<PageManager>(dir_ + "/idx_" + index_name + ".db", 32);
    idx.tree  = mk<BPlusTree>(*idx.pm);
    idx.wal   = mk<WAL>(dir_ + "/idx_" + index_name + ".wal");
    idx.wal->replay([&idx](std::int64_t k, std::int64_t v) {
        idx.tree->insert_dup(k, v);
    });

    indexes_.emplace(column, std::move(idx));
    index_names_[column] = index_name;
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
    save_catalog();
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

    open_index(st.index_name, col);
    SecondaryIndex& idx = indexes_[col];

    int populated = 0;
    tree_->scan([&](std::int64_t row_id, std::int64_t row_value) {
        std::int64_t key = (col == col_names_[0]) ? row_id : row_value;
        idx.wal->log_insert(key, row_id);
        idx.tree->insert_dup(key, row_id);
        ++populated;
    });
    idx.pm->sync();
    idx.wal->truncate();

    save_catalog();
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

    out << "1 строка\n";
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
    print_row_count(rows, out);
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
        out << "\n";
        print_row_count(1, out);
    } else {
        print_row_count(0, out);
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
    print_row_count(rows, out);
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
