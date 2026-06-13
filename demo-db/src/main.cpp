// REPL для mydb. psql-стиль:
//   mydb> SELECT * FROM items;
//   mydb> ...
//
// Multi-line ввод собирается до `;`. Meta-команды начинаются с точки:
//   .quit / .exit  — выход
//   .help          — справка
//   .history       — показать историю
//   .checkpoint    — синхронизировать диск
//
// История пишется в ~/.mydb_history.

#include "database.h"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

std::vector<std::string> g_history;

std::string history_path() {
    const char* home = std::getenv("HOME");
    if (!home) return "./.mydb_history";
    return std::string(home) + "/.mydb_history";
}

void history_load() {
    std::ifstream in(history_path());
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) g_history.push_back(line);
    }
}

void history_save() {
    std::ofstream out(history_path());
    if (!out.is_open()) return;
    for (const auto& l : g_history) out << l << "\n";
}

void history_add(const std::string& q) {
    if (q.empty()) return;
    if (!g_history.empty() && g_history.back() == q) return;
    g_history.push_back(q);
}

// Срезать пробелы по краям.
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Удалить SQL-комментарии: «-- до конца строки».
std::string strip_comment(const std::string& s) {
    auto pos = s.find("--");
    if (pos == std::string::npos) return s;
    return s.substr(0, pos);
}

void show_help() {
    std::cout << "Meta-команды:\n"
              << "  .help        справка\n"
              << "  .history     показать историю\n"
              << "  .checkpoint  принудительный sync на диск\n"
              << "  .quit/.exit  выход\n"
              << "\nSQL:\n"
              << "  CREATE TABLE <name> (<col> INT [PRIMARY KEY], <col> INT);\n"
              << "  CREATE INDEX <name> ON <table>(<column>);\n"
              << "  INSERT INTO <name> VALUES (<n>, <n>);\n"
              << "  SELECT <cols>|* FROM <name> [WHERE <col> <op> <n>];\n"
              << "  EXPLAIN SELECT ...;\n"
              << "\nКоманды многострочные, заверщаются `;`.\n";
}

void show_history() {
    std::size_t i = 1;
    for (const auto& q : g_history) {
        std::cout << "  " << i << "  " << q << "\n";
        ++i;
    }
}

bool handle_meta(const std::string& cmd, db::Database& db) {
    std::string c = trim(cmd);
    if (c == ".quit" || c == ".exit") return false;
    if (c == ".help") { show_help(); return true; }
    if (c == ".history") { show_history(); return true; }
    if (c == ".checkpoint") {
        db.checkpoint();
        std::cout << "(checkpoint done)\n";
        return true;
    }
    std::cout << "Неизвестная meta-команда: " << c << " (.help для справки)\n";
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string dir = "data/mydb";
    if (argc > 1) dir = argv[1];

    ::mkdir("data", 0755);
    std::cout << "mydb REPL — таблица 'items(id INT PRIMARY KEY, value INT)' по умолчанию.\n"
              << "Open: " << dir << "\n"
              << ".help для справки, .quit для выхода.\n\n";

    db::Database db(dir);
    history_load();

    std::string buffer;
    bool first_line = true;

    while (true) {
        std::cout << (first_line ? "mydb> " : "  ... ");
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        line = strip_comment(line);

        std::string t = trim(line);

        // Meta-команды только в начале (первой строке буфера).
        if (first_line && !t.empty() && t[0] == '.') {
            if (!handle_meta(t, db)) break;
            continue;
        }

        if (t.empty()) {
            if (first_line) continue;
            // продолжение пустой строкой — игнорируем
            continue;
        }

        if (!buffer.empty()) buffer += " ";
        buffer += t;
        first_line = false;

        // Проверяем, есть ли в накопленном `;`. Простейший вариант — ищем
        // последнюю точку с запятой. (Не учитываем строки с `;` внутри.)
        auto semi = buffer.find(';');
        if (semi == std::string::npos) {
            continue;   // ещё ждём
        }

        // Извлекаем statement до `;`, остаток — обратно в буфер.
        std::string stmt = buffer.substr(0, semi + 1);
        std::string rest = buffer.substr(semi + 1);

        try {
            db.execute(stmt, std::cout);
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
        history_add(trim(stmt));

        buffer = trim(rest);
        first_line = buffer.empty();
    }

    db.checkpoint();
    history_save();
    return 0;
}
