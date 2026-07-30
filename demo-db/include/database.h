#ifndef DB_DATABASE_H
#define DB_DATABASE_H

#include "btree.h"
#include "page_manager.h"
#include "sql.h"
#include "wal.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace db {

class Database {
public:
    explicit Database(const std::string& dir);

    void execute(const std::string& sql, std::ostream& out);
    void checkpoint();

private:
    struct SecondaryIndex {
        std::unique_ptr<PageManager> pm;
        std::unique_ptr<BPlusTree> tree;
        std::unique_ptr<WAL> wal;
        std::string column;
    };

    void execute_create(const Statement& st, std::ostream& out);
    void execute_create_index(const Statement& st, std::ostream& out);
    void execute_insert(const Statement& st, std::ostream& out);
    void execute_select(const Statement& st, std::ostream& out);

    bool matches_where(const Statement& st, std::int64_t id, std::int64_t value) const;

    // Простой query planner: возвращает имя плана.
    std::string plan_select(const Statement& st) const;
    void run_full_scan(const Statement& st, const std::vector<std::string>& cols,
                       std::ostream& out);
    void run_primary_eq(const Statement& st, const std::vector<std::string>& cols,
                        std::ostream& out);
    void run_secondary(const Statement& st, const std::vector<std::string>& cols,
                       const SecondaryIndex& idx, std::ostream& out);

    // Простейший каталог: имя таблицы, имена колонок и список индексов
    // хранятся в текстовом файле catalog.txt рядом с данными.
    void load_catalog();
    void save_catalog();
    void open_index(const std::string& index_name, const std::string& column);

    std::string dir_;
    std::unique_ptr<PageManager> pm_;
    std::unique_ptr<BPlusTree> tree_;
    std::unique_ptr<WAL> wal_;

    std::string table_name_;
    std::vector<std::string> col_names_;

    // Ключ — имя колонки, по которой построен индекс.
    std::unordered_map<std::string, SecondaryIndex> indexes_;
    // Имя колонки → имя индекса (нужно для каталога и имён файлов).
    std::unordered_map<std::string, std::string> index_names_;
};

}  // namespace db

#endif
