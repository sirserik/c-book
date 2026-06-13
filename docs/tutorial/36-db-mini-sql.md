# Глава 36. Мини-SQL

К этому моменту у нас уже есть рабочая СУБД: страничный файл, B+tree, WAL для durability. Но пользователю чтобы вставить запись, нужно писать на C++. Это не реально для применения. **SQL** — стандартный язык запросов, понятный пользователю и инструментам.

В этой главе мы напишем **мини-SQL** парсер и executor: `CREATE TABLE`, `INSERT`, `SELECT … WHERE`. После этого мы сможем сказать «`SELECT * FROM items WHERE value > 250`» и получить таблицу. Поверх нашего B+tree, со всеми гарантиями WAL.

Это четвёртая глава с парсингом в книге (мир в RPG, shell, save-формат, теперь SQL). Каждый раз — те же принципы, но более сложный язык. После этой главы у вас будет уверенность писать парсеры самостоятельно.

## Зачем SQL

В нашем коде `tree.insert(1, 100)` — это **низкоуровневый** API. Знаем тип B+tree, знаем структуру. Если бы захотели поменять движок (например, на hash) — переписали бы код пользователя.

SQL — **декларативный** язык. Вы говорите **что** вам нужно, не **как**:

```sql
SELECT * FROM items WHERE value > 250;
```

Это «выбери всё из items, где value больше 250». Как именно — заботит **executor**: может полным scan'ом, может через индекс, может через комбинацию. Программа меняется — SQL не трогаешь.

Это **разделение интерфейса и реализации**. Главная идея реляционных СУБД.

## Грамматика мини-SQL

Что поддержим:

```
CREATE TABLE <name> (<col> INT [PRIMARY KEY], <col> INT [PRIMARY KEY], ...)
INSERT INTO <name> VALUES (<num>, <num>, ...)
SELECT <col> | * [, <col> ...] FROM <name> [WHERE <col> <op> <num>]
```

Операторы сравнения: `=`, `!=`, `<`, `<=`, `>`, `>=`.

Что **не** поддержим (для простоты):
- Несколько таблиц одновременно (одна на БД).
- DELETE, UPDATE, JOIN.
- Сложные WHERE (`AND`, `OR`, `()`).
- Строковые колонки (только INT).
- ORDER BY, LIMIT, GROUP BY.

Это **подмножество** реального SQL — достаточное для демонстрации архитектуры.

## Архитектура

Три фазы обработки запроса:

```
SQL-строка
    ↓
[Tokenizer]   — разбивает на токены (keywords, identifiers, numbers, operators)
    ↓
[Parser]      — строит AST (Statement struct)
    ↓
[Executor]    — выполняет, обращается к B+tree/WAL, выводит результат
```

Тот же стек, что в shell parser (глава 29), только на другом языке.

## Tokenizer

Алгоритм похож на shell-tokenizer, но проще (нет кавычек):

```cpp
enum class TokenType {
    kIdent,    // foo, bar — имена таблиц и колонок
    kKeyword,  // CREATE, TABLE, SELECT, ... — зарезервированные слова
    kNumber,   // 42, -100
    kStar,     // *
    kComma,
    kLParen,
    kRParen,
    kOp,       // =, <, >, <=, >=, !=
    kSemi,
    kEnd
};
```

В цикле по символам:

```cpp
while (i < s.size()) {
    char c = s[i];
    if (isspace(c)) { ++i; continue; }

    // Спец-символы — отдельные токены
    if (c == '(') { out.push_back({kLParen, "("}); ++i; continue; }
    // ... остальные ...

    // Операторы сравнения: 1 или 2 символа
    if (c == '=' || c == '<' || c == '>' || c == '!') {
        std::string op(1, c);
        if (i + 1 < s.size() && s[i + 1] == '=') { op += '='; i += 2; }
        else { ++i; }
        out.push_back({kOp, op});
        continue;
    }

    // Числа: цифры, опциональный минус впереди
    if (isdigit(c) || (c == '-' && isdigit(s[i+1]))) {
        // ... сканируем до конца числа ...
        out.push_back({kNumber, ...});
        continue;
    }

    // Идентификатор или keyword
    if (isalpha(c) || c == '_') {
        // ... сканируем буквы/цифры/_ ...
        std::string text = s.substr(...);
        std::string upper = to_upper(text);
        if (is_keyword(upper)) {
            out.push_back({kKeyword, upper});
        } else {
            out.push_back({kIdent, text});
        }
        continue;
    }
    throw SqlError("неизвестный символ");
}
out.push_back({kEnd, ""});
```

Главные моменты:

**Operators**: `=`, `<`, `>`, `!` могут быть односимвольными или с `=` следующим (`<=`, `>=`, `!=`). Смотрим следующий символ для решения.

**Numbers** могут начинаться с минуса. Проверяем условие отдельно. Парсинг — до первого нецифрового.

**Keywords vs идентификаторы**: SQL **регистронезависимый** для keywords (`SELECT` == `select`). Приводим к верхнему регистру и сверяем со списком. Если есть — keyword. Иначе — идентификатор (имя колонки/таблицы).

Идентификаторы у нас регистрозависимые (как в большинстве баз — но опять, в Oracle и PostgreSQL разные правила).

**`kEnd`** в конце — sentinel, парсер на нём останавливается без проверки границ.

### Список keywords

```cpp
static const std::vector<std::string> kw = {
    "CREATE", "TABLE", "INT", "PRIMARY", "KEY",
    "INSERT", "INTO", "VALUES",
    "SELECT", "FROM", "WHERE",
};
```

Минимальный набор. Реальный SQL имеет сотни keywords.

## Parser

**Recursive descent** парсер. Для каждой правила грамматики — функция.

```cpp
struct Parser {
    const std::vector<Token>& tokens;
    std::size_t pos = 0;

    const Token& peek() const { return tokens[pos]; }
    Token consume() { return tokens[pos++]; }
    Token expect(Token::Type t);
    Token expect_keyword(const std::string& kw);
    std::int64_t parse_number();

    Statement parse();
    Statement parse_create();
    Statement parse_insert();
    Statement parse_select();
};
```

`peek()` — посмотреть следующий токен без потребления. `consume()` — потребить и продвинуть позицию. `expect(...)` — потребить, проверив тип/значение.

### Главная функция parse

```cpp
Statement parse() {
    if (peek().type != Token::kKeyword) throw SqlError("ожидался keyword");
    const std::string& kw = peek().text;
    if (kw == "CREATE") return parse_create();
    if (kw == "INSERT") return parse_insert();
    if (kw == "SELECT") return parse_select();
    throw SqlError("неизвестный statement: " + kw);
}
```

Простая диспетчеризация по первому ключевому слову.

### parse_create

```cpp
Statement parse_create() {
    Statement st;
    st.kind = Statement::kCreate;
    expect_keyword("CREATE");
    expect_keyword("TABLE");
    st.table = expect(Token::kIdent).text;
    expect(Token::kLParen);
    while (true) {
        std::string col = expect(Token::kIdent).text;
        expect_keyword("INT");
        if (peek().type == Token::kKeyword && peek().text == "PRIMARY") {
            consume();
            expect_keyword("KEY");
        }
        st.columns.push_back({col, "INT"});
        if (peek().type == Token::kComma) { consume(); continue; }
        break;
    }
    expect(Token::kRParen);
    if (peek().type == Token::kSemi) consume();
    return st;
}
```

Линейно идём по структуре: keyword «CREATE», keyword «TABLE», идентификатор таблицы, `(`, колонки через запятую, `)`, опциональный `;`.

Каждая колонка: идентификатор + `INT` + опциональный `PRIMARY KEY`. PRIMARY KEY игнорируется — у нас и так первая колонка primary.

`while (true)` + `break` — типичная идиома для «обрабатываем элементы через разделитель». Прочитали один → если запятая, продолжаем; иначе выходим.

### parse_select

```cpp
Statement parse_select() {
    Statement st;
    st.kind = Statement::kSelect;
    expect_keyword("SELECT");
    if (peek().type == Token::kStar) {
        consume();
        st.select_columns.push_back("*");
    } else {
        while (true) {
            st.select_columns.push_back(expect(Token::kIdent).text);
            if (peek().type == Token::kComma) { consume(); continue; }
            break;
        }
    }
    expect_keyword("FROM");
    st.table = expect(Token::kIdent).text;
    if (peek().type == Token::kKeyword && peek().text == "WHERE") {
        consume();
        st.has_where = true;
        st.where_col = expect(Token::kIdent).text;
        st.where_op = expect(Token::kOp).text;
        st.where_val = parse_number();
    }
    if (peek().type == Token::kSemi) consume();
    return st;
}
```

`SELECT * FROM …` или `SELECT col1, col2 FROM …`. Потом FROM и таблица. Потом опциональный WHERE.

WHERE поддерживает только `<col> <op> <number>`. Никаких `AND`/`OR`/скобок. Если их нужно — отдельный мини-парсер выражений (часто рекурсивный, с приоритетами).

## Recursive descent в общем

Идея recursive descent:
- Для каждого правила грамматики — функция.
- Функция читает токены и при необходимости рекурсивно вызывает другие функции.
- Возвращает соответствующий узел AST.

Это самый простой вариант парсера. Подходит для несложных грамматик. Для сложных (выражения с приоритетами, ambiguity) — есть **Pratt parsing**, **shift-reduce** (`bison`/`yacc`), **PEG** (`peg.js`).

Все профессиональные SQL-парсеры (PostgreSQL, MySQL, SQLite) — вручную написанные recursive descent или yacc-generated. Идея та же, что у нас, только грамматика в десятки раз больше.

## AST как одна структура

Я выбрал **один `Statement` struct** со всеми возможными полями. Альтернатива — полиморфная иерархия (`CreateStmt : Statement`, `InsertStmt : Statement`, и так далее). Полиморфизм гибче для расширения. Один struct проще для маленькой грамматики.

```cpp
struct Statement {
    enum Kind { kCreate, kInsert, kSelect };
    Kind kind;

    std::string table;
    std::vector<std::pair<std::string, std::string>> columns;  // для CREATE
    std::vector<std::int64_t> values;  // для INSERT
    std::vector<std::string> select_columns;
    bool has_where = false;
    std::string where_col;
    std::string where_op;
    std::int64_t where_val = 0;
};
```

«Лишние» поля для других kind просто пустые. На производительность это не влияет — `Statement` короткоживущий объект.

## Executor

Берёт `Statement`, выполняет:

```cpp
void Database::execute(const std::string& sql, std::ostream& out) {
    Statement st = parse_sql(sql);
    switch (st.kind) {
        case Statement::kCreate: execute_create(st, out); break;
        case Statement::kInsert: execute_insert(st, out); break;
        case Statement::kSelect: execute_select(st, out); break;
    }
}
```

### CREATE TABLE

```cpp
void Database::execute_create(const Statement& st, std::ostream& out) {
    if (st.columns.size() != 2) {
        throw SqlError("эта мини-СУБД поддерживает таблицы только из 2 колонок");
    }
    table_name_ = st.table;
    col_names_ = {st.columns[0].first, st.columns[1].first};
    out << "OK (table '" << table_name_ << "')\n";
}
```

Сохраняем имя таблицы и колонок в полях `Database`. **Не персистим** на диск — в этой версии схема живёт только в памяти, после рестарта забывается.

В настоящей СУБД схема хранится в системной таблице (например, `pg_class` в PostgreSQL).

### INSERT

```cpp
void Database::execute_insert(const Statement& st, std::ostream& out) {
    if (st.table != table_name_) {
        throw SqlError("неизвестная таблица: " + st.table);
    }
    if (st.values.size() != 2) {
        throw SqlError("ожидается 2 значения");
    }
    std::int64_t id = st.values[0];
    std::int64_t value = st.values[1];

    wal_->log_insert(id, value);   // durable
    tree_->insert(id, value);      // в кэше
    out << "1 row\n";
}
```

Берём первое значение как key (id), второе как value. Логируем в WAL (с fsync), вставляем в B+tree. Всё durable, всё чисто.

### SELECT

Самая интересная часть:

```cpp
void Database::execute_select(const Statement& st, std::ostream& out) {
    if (st.table != table_name_) throw SqlError(...);

    bool star = (st.select_columns.size() == 1 && st.select_columns[0] == "*");
    std::vector<std::string> cols = star ? col_names_ : st.select_columns;

    // Заголовок таблицы
    print_header(cols, out);

    // Оптимизация: если фильтр по primary key с `=` — используем find вместо scan.
    if (st.has_where && st.where_col == col_names_[0] && st.where_op == "=") {
        std::int64_t v = 0;
        if (tree_->find(st.where_val, v)) {
            print_row(cols, st.where_val, v, out);
            out << "(1 row)\n";
        } else {
            out << "(0 rows)\n";
        }
        return;
    }

    // Иначе — полный scan с фильтром.
    int rows = 0;
    tree_->scan([&](std::int64_t id, std::int64_t value) {
        if (!matches_where(st, id, value)) return;
        print_row(cols, id, value, out);
        ++rows;
    });
    out << "(" << rows << " rows)\n";
}
```

Главная **оптимизация** — для `WHERE id = X` использовать `tree_->find(X)` вместо полного scan. Это разница между O(log N) и O(N).

Это уже **простой query planner** — выбор стратегии выполнения по форме запроса. В реальной СУБД — десятки оптимизаций (использовать ли индекс, в каком порядке join'ить таблицы, и так далее). Тема главы 37.

`matches_where` — функция фильтрации:

```cpp
bool Database::matches_where(const Statement& st,
                             std::int64_t id, std::int64_t value) const {
    if (!st.has_where) return true;
    std::int64_t left = (st.where_col == col_names_[0]) ? id : value;
    const auto& op = st.where_op;
    const auto r = st.where_val;
    if (op == "=")  return left == r;
    if (op == "!=") return left != r;
    if (op == "<")  return left <  r;
    if (op == "<=") return left <= r;
    if (op == ">")  return left >  r;
    if (op == ">=") return left >= r;
    throw SqlError("неизвестный оператор: " + op);
}
```

## Демо

```bash
$ ./build/mydb
> CREATE TABLE items (id INT PRIMARY KEY, value INT);
OK (table 'items')

> INSERT INTO items VALUES (1, 100);
1 row

> INSERT INTO items VALUES (2, 200);
1 row

> INSERT INTO items VALUES (3, 300);
1 row

> SELECT * FROM items;
id       | value   
---------+---------
1        | 100     
2        | 200     
3        | 300     
4        | 400     
5        | 500     
(5 rows)

> SELECT * FROM items WHERE id = 3;
id       | value   
---------+---------
3        | 300     
(1 row)

> SELECT * FROM items WHERE value > 250;
id       | value   
---------+---------
3        | 300     
4        | 400     
5        | 500     
(3 rows)

> SELECT id FROM items WHERE value <= 200;
id      
--------
1       
2       
(2 rows)
```

Полноценный SQL на нашем движке. Запросы парсятся, executor применяет правильные операции, B+tree даёт результаты, всё работает.

## Архитектура «парсер-исполнитель»

Если присмотреться, наш `Database::execute` — это **компилятор и интерпретатор** в одной упаковке:

```
SQL-строка → токены → AST → действия на B+tree
```

То же делает любой интерпретируемый язык (Python, JS). Те же три фазы.

Разница: настоящий SQL-компилятор **не** интерпретирует AST напрямую. Он:
1. Парсит в AST.
2. **Анализирует семантику**: проверяет, что таблицы существуют, типы совместимы.
3. **Оптимизирует**: переписывает AST в более эффективную форму (плана).
4. **Генерирует план**: дерево физических операторов (TableScan, IndexLookup, Filter, Sort, Join, ...).
5. **Выполняет** план через volcano-model (`next()` рекурсивно).

Это полноценный **query optimizer**. У нас он минимален — одна оптимизация для `WHERE pkey = const`.

В главе 37 (индексы и план запроса) добавим больше.

## Что не получится по нашей грамматике

Несколько примеров запросов, которые **не сработают**:

- `SELECT * FROM items WHERE id = 1 AND value > 100` — мы не понимаем `AND`.
- `SELECT * FROM items ORDER BY value` — нет ORDER BY.
- `SELECT * FROM items LIMIT 5` — нет LIMIT.
- `DELETE FROM items WHERE id = 1` — нет DELETE.
- `UPDATE items SET value = 999 WHERE id = 1` — нет UPDATE.
- `SELECT COUNT(*) FROM items` — нет функций.
- `SELECT a.id, b.value FROM a JOIN b ON a.id = b.id` — нет JOIN.

Каждое — отдельное расширение парсера и executor. Хороший выпуск для домашних упражнений.

## Главные правила главы

1. **Три фазы**: tokenize → parse → execute. Стандарт для языковых процессоров.
2. **Recursive descent** для простых грамматик — каждое правило = функция.
3. **`peek()`/`consume()`/`expect()`** — типичные методы парсера.
4. **Keywords case-insensitive** в SQL. Уменьшайте/увеличивайте на этапе токенизации.
5. **AST как struct** с kind-полем для маленьких грамматик; полиморфизм для больших.
6. **Простейший query planner** — оптимизация частых случаев (например, lookup через primary key).
7. **Сообщения об ошибках с местом** — пользователь должен знать, где не парсится.
8. **Декларативный язык** vs низкоуровневый API — главное преимущество SQL.

## Маленькое упражнение

1. Запустите. Попробуйте свои запросы. Какие работают, какие нет?

2. Добавьте оператор `<>` как синоним `!=`. Это стандартный SQL.

3. Добавьте `LIMIT N` после WHERE — ограничение числа строк в результате.

4. Добавьте `ORDER BY col` — наш B+tree уже даёт отсортированный обход по ключу. Для не-primary колонки нужно собрать все в vector и `std::sort`.

5. Добавьте `DELETE FROM <table> WHERE <col> = <num>`. Понадобится `tree.remove(key)` (упражнение из главы 34).

6. (Сложнее) Расширьте WHERE на `AND`/`OR` с скобками. Подсказка: грамматика выражений (recursive descent с приоритетами или Pratt parsing).

7. (Сложнее) Добавьте `UPDATE … SET col = num WHERE …`. Это find → modify → insert.

8. (Очень сложно) Поддержите строковые колонки. B+tree должен научиться хранить переменные значения; формат страницы становится «slotted page».

## Что дальше

Глава 37 — **индексы и план запроса**. Сейчас у нас primary index на id (через B+tree). Добавим **secondary index** — отдельный B+tree, отображающий, например, `value → id`. Тогда `WHERE value = X` тоже сможем найти за O(log N) вместо O(N).

Глава 38 — REPL-клиент (как `psql` или `mysql` shell). Будут стрелки вверх для истории, многострочный ввод, цвет.

Глава 39 — бенчмарки и итог Части IV.
