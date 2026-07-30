// Тесты мини-СУБД: страничный менеджер, B+дерево, WAL, парсер SQL.
// Сборка и запуск:  make tests && ./build/tests/test_db
#include "btree.h"
#include "database.h"
#include "page_manager.h"
#include "sql.h"
#include "wal.h"

#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cout << "FAIL  " << what << "\n";
    }
}

template <typename A, typename B>
void check_eq(const A& got, const B& expected, const std::string& what) {
    ++checks;
    if (!(got == expected)) {
        ++failures;
        std::cout << "FAIL  " << what << ": получили " << got
                  << ", ждали " << expected << "\n";
    }
}

std::string tmp_path(const std::string& name) {
    return "/tmp/mydb_test_" + name;
}

void remove_file(const std::string& path) {
    ::unlink(path.c_str());
}

// === PageManager ===

void test_page_manager() {
    std::string path = tmp_path("pm.db");
    remove_file(path);

    {
        db::PageManager pm(path, /*cache_size=*/4);
        check_eq(pm.page_count(), 0u, "новый файл пуст");

        db::PageId a = pm.allocate_page();
        db::PageId b = pm.allocate_page();
        check_eq(a, 0u, "первая страница получает номер 0");
        check_eq(b, 1u, "вторая — номер 1");
        check_eq(pm.page_count(), 2u, "page_count после двух allocate");

        db::Page p = db::make_page();
        p[0] = 'A';
        p[db::PAGE_SIZE - 1] = 'Z';
        pm.write_page(a, p);

        db::Page q = pm.read_page(a);
        check_eq(static_cast<char>(q[0]), 'A', "первый байт страницы");
        check_eq(static_cast<char>(q[db::PAGE_SIZE - 1]), 'Z', "последний байт страницы");
        pm.sync();
    }

    // Данные переживают закрытие файла.
    {
        db::PageManager pm(path, 4);
        check_eq(pm.page_count(), 2u, "page_count восстановлен из размера файла");
        db::Page q = pm.read_page(0);
        check_eq(static_cast<char>(q[0]), 'A', "байт пережил переоткрытие");
    }

    // Кэш меньше числа страниц — вытеснение не должно терять данные.
    {
        db::PageManager pm(path, /*cache_size=*/2);
        for (int i = 0; i < 10; ++i) {
            db::PageId pid = pm.allocate_page();
            db::Page p = db::make_page();
            p[0] = static_cast<unsigned char>('0' + i);
            pm.write_page(pid, p);
        }
        pm.sync();
        bool all_ok = true;
        for (int i = 0; i < 10; ++i) {
            db::Page p = pm.read_page(static_cast<db::PageId>(2 + i));
            if (p[0] != static_cast<unsigned char>('0' + i)) all_ok = false;
        }
        check(all_ok, "вытеснение из LRU не теряет записанное");
    }

    // Чтение за границей файла — ошибка, а не мусор.
    {
        db::PageManager pm(path, 4);
        bool threw = false;
        try {
            pm.read_page(9999);
        } catch (const db::PageError&) {
            threw = true;
        }
        check(threw, "чтение несуществующей страницы бросает PageError");
    }

    remove_file(path);
}

// === B+дерево ===

void test_btree_basic() {
    std::string path = tmp_path("btree.db");
    remove_file(path);

    db::PageManager pm(path, 32);
    db::BPlusTree tree(pm);

    std::int64_t v = 0;
    check(!tree.find(42, v), "поиск в пустом дереве не находит ничего");

    // 40 ключей вперемешку — гарантированно вызывает несколько split'ов
    // при MAX_KEYS = 4.
    std::vector<std::int64_t> keys;
    for (int i = 0; i < 40; ++i) keys.push_back((i * 17) % 40);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        tree.insert(keys[i], keys[i] * 100);
    }

    bool all_found = true;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        std::int64_t got = 0;
        if (!tree.find(keys[i], got) || got != keys[i] * 100) all_found = false;
    }
    check(all_found, "после 40 вставок находятся все ключи");

    check(!tree.find(1000, v), "несуществующий ключ не находится");

    // Обход должен идти по возрастанию и вернуть ровно 40 записей.
    std::vector<std::int64_t> seen;
    tree.scan([&](std::int64_t k, std::int64_t) { seen.push_back(k); });
    check_eq(seen.size(), static_cast<std::size_t>(40), "scan обошёл все записи");
    bool sorted = true;
    for (std::size_t i = 1; i < seen.size(); ++i) {
        if (seen[i - 1] >= seen[i]) sorted = false;
    }
    check(sorted, "scan идёт в порядке возрастания ключей");

    // Повторная вставка того же ключа заменяет значение.
    tree.insert(5, 999);
    std::int64_t got = 0;
    tree.find(5, got);
    check_eq(got, static_cast<std::int64_t>(999), "insert по существующему ключу заменяет значение");

    std::size_t count = 0;
    tree.scan([&](std::int64_t, std::int64_t) { ++count; });
    check_eq(count, static_cast<std::size_t>(40), "замена не добавила новую запись");

    remove_file(path);
}

void test_btree_persistence() {
    std::string path = tmp_path("btree_persist.db");
    remove_file(path);

    {
        db::PageManager pm(path, 16);
        db::BPlusTree tree(pm);
        for (int i = 1; i <= 50; ++i) tree.insert(i, i * 2);
        pm.sync();
    }
    {
        db::PageManager pm(path, 16);
        db::BPlusTree tree(pm);
        bool ok = true;
        for (int i = 1; i <= 50; ++i) {
            std::int64_t v = 0;
            if (!tree.find(i, v) || v != i * 2) ok = false;
        }
        check(ok, "дерево целиком читается после переоткрытия файла");
    }

    remove_file(path);
}

void test_btree_duplicates() {
    std::string path = tmp_path("btree_dup.db");
    remove_file(path);

    db::PageManager pm(path, 32);
    db::BPlusTree tree(pm);

    // 12 записей с одним и тем же ключом — при MAX_KEYS = 4 они обязаны
    // разъехаться по нескольким листьям, в том числе через разделитель.
    for (int i = 1; i <= 12; ++i) tree.insert_dup(7, i);
    tree.insert_dup(3, 100);
    tree.insert_dup(9, 200);

    std::vector<std::int64_t> found;
    tree.find_all(7, [&](std::int64_t v) { found.push_back(v); });
    check_eq(found.size(), static_cast<std::size_t>(12), "find_all находит все дубликаты");

    std::set<std::int64_t> unique(found.begin(), found.end());
    check_eq(unique.size(), static_cast<std::size_t>(12), "дубликаты не затирают друг друга");

    std::vector<std::int64_t> one;
    tree.find_all(3, [&](std::int64_t v) { one.push_back(v); });
    check_eq(one.size(), static_cast<std::size_t>(1), "одиночный ключ находится ровно один раз");

    std::vector<std::int64_t> none;
    tree.find_all(5, [&](std::int64_t v) { none.push_back(v); });
    check(none.empty(), "отсутствующий ключ не даёт ничего");

    remove_file(path);
}

// === WAL ===

void test_wal() {
    std::string path = tmp_path("wal.log");
    remove_file(path);

    {
        db::WAL wal(path);
        wal.log_insert(1, 10);
        wal.log_insert(2, 20);
        wal.log_insert(3, 30);
        check_eq(wal.size_on_disk(), static_cast<std::size_t>(3 * 25),
                 "три записи по 25 байт");
    }

    {
        db::WAL wal(path);
        std::vector<std::pair<std::int64_t, std::int64_t>> got;
        wal.replay([&](std::int64_t k, std::int64_t v) { got.push_back(std::make_pair(k, v)); });
        check_eq(got.size(), static_cast<std::size_t>(3), "replay вернул три записи");
        check(got.size() == 3 && got[0].first == 1 && got[0].second == 10,
              "первая запись восстановлена верно");
        check(got.size() == 3 && got[2].first == 3 && got[2].second == 30,
              "последняя запись восстановлена верно");

        wal.truncate();
        check_eq(wal.size_on_disk(), static_cast<std::size_t>(0), "после truncate журнал пуст");
    }

    // Обрыв на середине записи: replay должен отдать целые записи и
    // остановиться, а не бросить и не выдумать данные.
    {
        remove_file(path);
        db::WAL wal(path);
        wal.log_insert(1, 10);
        wal.log_insert(2, 20);
    }
    {
        int fd = ::open(path.c_str(), O_WRONLY);
        ::ftruncate(fd, 25 + 10);   // вторая запись обрезана посередине
        ::close(fd);

        db::WAL wal(path);
        int count = 0;
        wal.replay([&](std::int64_t, std::int64_t) { ++count; });
        check_eq(count, 1, "оборванная запись в конце игнорируется");
    }

    // Испорченный байт внутри записи ловится контрольной суммой.
    {
        remove_file(path);
        {
            db::WAL wal(path);
            wal.log_insert(1, 10);
            wal.log_insert(2, 20);
        }
        int fd = ::open(path.c_str(), O_RDWR);
        unsigned char bad = 0xFF;
        ::pwrite(fd, &bad, 1, 25 + 6);   // портим ключ второй записи
        ::close(fd);

        db::WAL wal(path);
        int count = 0;
        wal.replay([&](std::int64_t, std::int64_t) { ++count; });
        check_eq(count, 1, "битая контрольная сумма обрывает replay");
    }

    remove_file(path);
}

// === Парсер SQL ===

void test_sql() {
    db::Statement s = db::parse_sql("CREATE TABLE items (id INT PRIMARY KEY, value INT)");
    check(s.kind == db::Statement::kCreate, "CREATE TABLE распознан");
    check_eq(s.table, std::string("items"), "имя таблицы");
    check_eq(s.columns.size(), static_cast<std::size_t>(2), "две колонки");

    s = db::parse_sql("INSERT INTO items VALUES (1, 100);");
    check(s.kind == db::Statement::kInsert, "INSERT распознан");
    check_eq(s.values.size(), static_cast<std::size_t>(2), "два значения");
    check_eq(s.values[0], static_cast<std::int64_t>(1), "первое значение");

    s = db::parse_sql("select * from items where value >= -5");
    check(s.kind == db::Statement::kSelect, "SELECT в нижнем регистре распознан");
    check(s.has_where, "WHERE распознан");
    check_eq(s.where_op, std::string(">="), "оператор >=");
    check_eq(s.where_val, static_cast<std::int64_t>(-5), "отрицательное число");

    s = db::parse_sql("EXPLAIN SELECT id FROM items WHERE id = 3");
    check(s.is_explain, "EXPLAIN распознан");
    check_eq(s.select_columns.size(), static_cast<std::size_t>(1), "одна колонка в списке");

    s = db::parse_sql("CREATE INDEX idx_val ON items (value)");
    check(s.kind == db::Statement::kCreateIndex, "CREATE INDEX распознан");
    check_eq(s.index_name, std::string("idx_val"), "имя индекса");

    const char* bad[] = {
        "SELECT",
        "INSERT INTO items VALUES (1",
        "SELECT * FROM items WHERE",
        "DROP TABLE items",
    };
    for (std::size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        bool threw = false;
        try {
            db::parse_sql(bad[i]);
        } catch (const db::SqlError&) {
            threw = true;
        }
        check(threw, std::string("плохой SQL отвергается: ") + bad[i]);
    }
}

// === Database целиком ===

void test_database() {
    std::string dir = tmp_path("dbdir");
    // Чистим каталог от прошлых прогонов.
    ::unlink((dir + "/data.db").c_str());
    ::unlink((dir + "/data.wal").c_str());
    ::unlink((dir + "/idx_idx_val.db").c_str());
    ::unlink((dir + "/idx_idx_val.wal").c_str());

    std::ostringstream out;
    {
        db::Database d(dir);
        d.execute("CREATE TABLE items (id INT PRIMARY KEY, value INT)", out);
        d.execute("INSERT INTO items VALUES (1, 100)", out);
        d.execute("INSERT INTO items VALUES (2, 100)", out);
        d.execute("INSERT INTO items VALUES (3, 300)", out);
        d.execute("CREATE INDEX idx_val ON items (value)", out);

        std::ostringstream sel;
        d.execute("SELECT * FROM items WHERE value = 100", sel);
        check(sel.str().find("(2 строки)") != std::string::npos,
              "вторичный индекс возвращает обе строки с одинаковым значением");

        std::ostringstream plan;
        d.execute("EXPLAIN SELECT * FROM items WHERE value = 100", plan);
        check(plan.str().find("SecondaryLookup") != std::string::npos,
              "планировщик выбирает вторичный индекс");

        std::ostringstream plan2;
        d.execute("EXPLAIN SELECT * FROM items WHERE id = 2", plan2);
        check(plan2.str().find("PrimaryLookup") != std::string::npos,
              "по первичному ключу выбирается PrimaryLookup");

        d.checkpoint();
    }

    // Данные переживают закрытие базы.
    {
        db::Database d(dir);
        std::ostringstream sel;
        d.execute("SELECT * FROM items WHERE id = 3", sel);
        check(sel.str().find("(1 строка)") != std::string::npos,
              "строка нашлась после переоткрытия базы");
    }

    ::unlink((dir + "/data.db").c_str());
    ::unlink((dir + "/data.wal").c_str());
    ::unlink((dir + "/idx_idx_val.db").c_str());
    ::unlink((dir + "/idx_idx_val.wal").c_str());
}

}  // namespace

int main() {
    test_page_manager();
    test_btree_basic();
    test_btree_persistence();
    test_btree_duplicates();
    test_wal();
    test_sql();
    test_database();

    std::cout << (failures ? "ЕСТЬ ОШИБКИ: " : "все проверки прошли: ")
              << (checks - failures) << "/" << checks << "\n";
    return failures ? 1 : 0;
}
