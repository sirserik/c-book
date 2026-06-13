# Глава 38. REPL-клиент

В предыдущих главах наш `main` запускал жёстко зашитый список SQL-запросов. Для настоящей СУБД это не годится — нужно интерактивное общение с пользователем. **REPL** (Read-Eval-Print Loop) — стандартный интерфейс таких программ. `psql`, `mysql`, `sqlite3` — все они REPL'ы.

В этой главе превратим `mydb` в полноценный REPL:
- Приглашение `mydb>`.
- Multi-line ввод (запрос на нескольких строках, до `;`).
- Meta-команды (`.help`, `.quit`, `.history`, `.checkpoint`).
- История в файл `~/.mydb_history`.
- Комментарии `-- до конца строки`.

Глава короткая — мы уже знаем все нужные инструменты из прошлых глав. Это **интеграция**.

## Что такое REPL

```
Read    — прочитать ввод пользователя
Eval    — вычислить (выполнить)
Print   — напечатать результат
Loop    — повторить
```

Простая идея. Главные элементы:

1. **Prompt** — приглашение, показывающее, что система ждёт ввода. У нас `mydb> `.
2. **Reader** — чтение строки/команды.
3. **Executor** — выполнение.
4. **Output** — печать результата.
5. **Loop** — повтор до выхода.

Что отличает «настоящий» REPL от тестового цикла:

- **Multi-line input**: команда может занимать несколько строк. Continuation prompt (`  ... ` у нас, `... ` у `psql`).
- **Meta-команды**: то, что не SQL (`.quit`, `.help`).
- **История** между сессиями.
- **Раскраска и автодополнение** — приятные дополнения.

Самые мощные REPL'ы (Python, Node) могут даже **запоминать переменные** между запросами, иметь автозавершение по имени функций, выводить контекстную справку. Это огромная инфраструктура; мы делаем минимум.

## Multi-line ввод

В SQL запрос завершается **точкой с запятой** `;`. Это позволяет разбивать длинные запросы:

```sql
mydb> SELECT *
  ...   FROM items
  ...   WHERE value > 250;
```

Алгоритм:
1. Накопить во **buffer** строки, разделённые пробелом.
2. После каждой строки проверять: есть ли в buffer `;`?
3. Если есть — извлечь statement до `;`, выполнить, остаток оставить в buffer.
4. Если нет — продолжить считывание.

```cpp
std::string buffer;
bool first_line = true;

while (true) {
    std::cout << (first_line ? "mydb> " : "  ... ");
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) break;
    line = strip_comment(line);

    std::string t = trim(line);
    if (t.empty() && first_line) continue;

    if (!buffer.empty()) buffer += " ";
    buffer += t;
    first_line = false;

    auto semi = buffer.find(';');
    if (semi == std::string::npos) continue;

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
```

`first_line` — флаг, чтобы показывать разный prompt: главный или продолжения.

`strip_comment` срезает `--`-комментарии: всё после `--` до конца строки удаляется.

После выполнения statement остаток (если что-то после `;`) кладётся обратно в buffer. Это позволяет:

```sql
mydb> SELECT * FROM items; SELECT * FROM items WHERE id = 1;
```

Сначала выполнится первый, потом второй.

### Ограничения

Простейший поиск `;` через `find` ломается если `;` **внутри строкового литерала**: `INSERT INTO items VALUES ('hello; world')`. Реальный shell/REPL учитывает контекст (квотинг). У нас в SQL нет строк, поэтому проблема не актуальна.

В **настоящем psql** парсинг даже сложнее — например, `;` внутри функции PL/pgSQL (`CREATE FUNCTION ... AS $$ ... $$;`). Они решают через **special delimiters** (`\$$`).

## Meta-команды

Команды, начинающиеся с **точки** — не SQL, а команды REPL'а. `psql` использует `\` (`\q`, `\d`), `sqlite3` — `.` (`.quit`, `.tables`). Мы за `sqlite3`-стиль.

```cpp
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
    std::cout << "Неизвестная meta-команда: " << c << "\n";
    return true;
}
```

Возвращает `false` — пользователь хочет выйти. `true` — продолжить REPL.

В главном цикле:

```cpp
if (first_line && !t.empty() && t[0] == '.') {
    if (!handle_meta(t, db)) break;
    continue;
}
```

Meta-команда обрабатывается **только в начале**, не в продолжении (где buffer уже не пуст).

## История

История запросов — стандартное удобство. Стрелка вверх (с readline) или просто список через `.history`.

```cpp
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
```

То же, что в shell (глава 30). Загружаем при старте, сохраняем при выходе, не дублируем подряд идущие.

`.history`:
```cpp
void show_history() {
    std::size_t i = 1;
    for (const auto& q : g_history) {
        std::cout << "  " << i << "  " << q << "\n";
        ++i;
    }
}
```

## Comments

SQL-комментарии — `--` до конца строки. Удаляем перед буферизацией:

```cpp
std::string strip_comment(const std::string& s) {
    auto pos = s.find("--");
    if (pos == std::string::npos) return s;
    return s.substr(0, pos);
}
```

Не учитывает `--` внутри строк (опять, у нас строк нет).

В SQL есть и **многострочные** `/* ... */`. Не реализуем для простоты.

## Запуск

```bash
$ ./build/mydb
mydb REPL — таблица 'items(id INT PRIMARY KEY, value INT)' по умолчанию.
Open: data/mydb
.help для справки, .quit для выхода.

mydb> CREATE TABLE items (id INT PRIMARY KEY, value INT);
OK (table 'items')

mydb> INSERT INTO items VALUES (1, 100);
1 row

mydb> SELECT *
  ...   FROM items;
id       | value   
---------+---------
1        | 100     
(1 rows)

mydb> CREATE INDEX idx ON items(value);
Index 'idx' on value (1 rows indexed)

mydb> EXPLAIN SELECT * FROM items WHERE value = 100;
Plan: SecondaryLookup(value=100)

mydb> .history
  1  CREATE TABLE items (id INT PRIMARY KEY, value INT);
  2  INSERT INTO items VALUES (1, 100);
  3  SELECT * FROM items;
  4  CREATE INDEX idx ON items(value);
  5  EXPLAIN SELECT * FROM items WHERE value = 100;

mydb> .checkpoint
(checkpoint done)

mydb> .quit
$ cat ~/.mydb_history
CREATE TABLE items (id INT PRIMARY KEY, value INT);
INSERT INTO items VALUES (1, 100);
SELECT * FROM items;
CREATE INDEX idx ON items(value);
EXPLAIN SELECT * FROM items WHERE value = 100;
```

Multi-line через `SELECT *\n  FROM items;` собирает запрос. История сохранена в файл.

## Параметры командной строки

```cpp
int main(int argc, char* argv[]) {
    std::string dir = "data/mydb";
    if (argc > 1) dir = argv[1];
    // ...
}
```

`./build/mydb data/customer_db` — открывает другую БД. Это полезно для нескольких баз на одной машине.

В psql есть много параметров: `-c "query"` для одной команды, `-f file.sql` для файла, `-h host` для удалённого подключения. Мы — минимум.

## Стрелки вверх/вниз

Чтобы стрелка вверх возвращала предыдущую команду, нужно:
1. Включить **raw mode** для терминала (`tcsetattr` с отключённым `ICANON`).
2. Читать **по одному символу**.
3. Распознавать **escape-sequences**: стрелка вверх это `\033[A`, вниз — `\033[B`, влево — `\033[D`, вправо — `\033[C`.
4. Поддерживать **курсор**: писать символ, потом обновлять позицию.
5. Восстановить **canonical mode** при выходе.

Это **много кода** на сырых termios — мы коснёмся этой темы в shell (глава 30 говорилa: «raw termios», не реализовали).

**Готовое решение** — библиотеки:
- **GNU readline** — самая популярная. Используется bash, gdb, psql. LGPL.
- **libedit** (BSD) — лицензионная альтернатива.
- **linenoise** — крошечная (~1000 строк C), используется sqlite, redis.

Подключение readline:
```cpp
#include <readline/readline.h>
#include <readline/history.h>

while (true) {
    char* line = readline(first_line ? "mydb> " : "  ... ");
    if (!line) break;
    std::string sline = line;
    free(line);
    add_history(sline.c_str());
    // ... process sline ...
}
write_history(history_path().c_str());
```

И добавить `-lreadline` к линкеру.

У нас — без readline. Стрелка вверх не сработает, но `.history` показывает список.

## Раскраска

`psql` иногда подсвечивает: ключевые слова синим, строки зелёным. Это делается через **ANSI escape codes**:

```cpp
const char* RED   = "\033[31m";
const char* GREEN = "\033[32m";
const char* RESET = "\033[0m";

std::cout << RED << "Error: " << RESET << e.what();
```

Терминал интерпретирует `\033[31m` (ESC + `[31m`) как «начни писать красным», `\033[0m` — «сбрось». В файле или non-terminal stdout эти sequences просто видны как байты — мусор. Поэтому опытные программы проверяют `isatty(STDOUT_FILENO)` и раскрашивают только в терминале.

Мы — без цвета, чтобы не усложнять.

## Архитектура

Структура нашего REPL:

```
main loop
├── prompt (mydb> или   ... )
├── getline
├── strip_comment
├── meta-command? → handle_meta → continue
├── append to buffer
├── ';' in buffer? → 
│   ├── split: stmt + rest
│   ├── db.execute(stmt) → output
│   ├── history_add(stmt)
│   └── buffer = rest
└── repeat

on exit:
├── db.checkpoint()
└── history_save()
```

Простая, прозрачная. Те же концепции, что в shell (глава 30) — отличаются только конкретные команды.

## Главные правила главы

1. **REPL = Read+Eval+Print+Loop.** Базовая модель интерактивной программы.
2. **Multi-line через накопительный buffer и `;` как terminator.**
3. **Meta-команды (`.` или `\`-prefix)** отдельно от языка БД.
4. **История** в `~/.<app>_history`. Не дублируем подряд.
5. **Комментарии `--`** удаляются до парсинга.
6. **readline для стрелочек и автодополнения.** Самим писать — сложно.
7. **Раскраска через ANSI** — только в `isatty`.
8. **Параметры командной строки** (`argv`) — для разных БД, для batch-execution.

## Маленькое упражнение

1. Запустите. Поиграйтесь с командами. Попробуйте многострочные.

2. Запустите снова — увидите, что данные сохранены и `.history` помнит прошлые команды.

3. Добавьте meta-команду `.schema` — печатает текущую таблицу и колонки.

4. Добавьте `.tables` — список всех таблиц (у нас одна).

5. (Сложнее) Реализуйте `.read file.sql` — выполнить SQL из файла.

6. (Сложнее) Добавьте параметр `-c "query"` — выполнить одну команду и выйти. Полезно для скриптов.

7. (Сложнее) Подключите libreadline. Получите стрелки вверх/вниз, Ctrl+R, автодополнение по keywords.

8. (Очень сложно) Добавьте раскраску ключевых слов в результате `.history` через ANSI codes.

## Что дальше

Глава 39 — **бенчмарки и финал Части IV**. Замерим производительность нашей мини-СУБД на тысячах вставок, сравним поведение с/без индекса, обсудим, что осталось «за кадром» (delete, update, transactions, концурентность, расширенные SQL-фичи).

После этого Часть IV закроется, и мы перейдём к **Части V — TCP-чат** (главы 40-47): сетевое программирование, многопоточность, реактор-цикл.
