# Глава 37. Индексы и план запроса

В прошлой главе наш SQL-движок имел **одну** оптимизацию: `WHERE id = X` шёл через `tree.find()` за O(log N). Все остальные WHERE — full scan O(N). Это значит, на миллионе записей запрос `WHERE value = 200` будет проходить миллион ключей. Медленно.

Решение — **secondary index**: ещё один B+tree, отображающий `value → id`. Тогда поиск по value тоже O(log N).

В этой главе добавим:
- `CREATE INDEX idx_name ON table(column)` — создание secondary index.
- Автоматическое обновление индекса на INSERT.
- **Query planner** — выбор стратегии: primary lookup / secondary lookup / secondary scan / full scan.
- `EXPLAIN SELECT ...` — показать план запроса без выполнения.

## Primary vs secondary index

**Primary index** — по primary key. В нашем случае — на `id`. Структура B+tree, где key = id, value = ...что-то связанное с записью (у нас просто `value`).

**Secondary index** — на любую другую колонку. Отдельный B+tree, где key = значение колонки, value = primary key. Для поиска по value: `secondary.find(200) → id → primary.find(id) → full row`.

Это два **отдельных** B+tree, каждый со своими страницами на диске. Каждый INSERT в основную таблицу должен обновить **оба** дерева.

В PostgreSQL это так же: одна таблица + N индексов. У каждого индекса — свой файл. CREATE TABLE создаёт primary, CREATE INDEX добавляет secondary.

### Цена индексов

Индексы — **двойной меч**:

**Плюсы**: быстрый поиск по индексированным колонкам.

**Минусы**:
- **Запись медленнее**: каждый INSERT обновляет все индексы.
- **Место на диске**: каждый индекс — отдельный B+tree.
- **WAL больше**: каждое обновление логируется и для основной таблицы, и для каждого индекса.

Поэтому в production: индексируйте **только то, по чему часто фильтруете**. Избыточные индексы тормозят запись без пользы для чтения.

## Структура secondary index в коде

```cpp
struct SecondaryIndex {
    std::unique_ptr<PageManager> pm;
    std::unique_ptr<BPlusTree> tree;
    std::unique_ptr<WAL> wal;
    std::string column;
};

std::unordered_map<std::string, SecondaryIndex> indexes_;
```

Каждый индекс — **полноценная мини-СУБД**: свой `PageManager`, свой B+tree, свой WAL. Файлы — `idx_<name>.db` и `idx_<name>.wal` в директории базы.

Когда выполняется `CREATE INDEX idx_val ON items(value)`:

```cpp
void Database::execute_create_index(const Statement& st, std::ostream& out) {
    const std::string& col = st.columns[0].first;

    SecondaryIndex idx;
    idx.column = col;
    idx.pm    = mk<PageManager>(dir_ + "/idx_" + st.index_name + ".db", 32);
    idx.tree  = mk<BPlusTree>(*idx.pm);
    idx.wal   = mk<WAL>(dir_ + "/idx_" + st.index_name + ".wal");
    idx.wal->replay([&idx](std::int64_t k, std::int64_t v) {
        idx.tree->insert(k, v);
    });

    // Populate from existing rows.
    int populated = 0;
    tree_->scan([&](std::int64_t row_id, std::int64_t row_value) {
        std::int64_t key = (col == col_names_[0]) ? row_id : row_value;
        idx.wal->log_insert(key, row_id);
        idx.tree->insert(key, row_id);
        ++populated;
    });
    idx.pm->sync();
    idx.wal->truncate();

    indexes_.emplace(col, std::move(idx));
}
```

Алгоритм:
1. Создать новый PageManager + B+tree + WAL для индекса.
2. Replay WAL индекса (на случай, если индекс существовал ранее).
3. **Index build**: пройтись по всем строкам в основной таблице, для каждой вставить `(value, id)` в индекс.
4. Сделать checkpoint индекса (sync + truncate WAL).
5. Зарегистрировать в `indexes_`.

Этот **index build** в реальной СУБД может занять часы на миллионах строк. PostgreSQL умеет `CREATE INDEX CONCURRENTLY` — построение индекса параллельно с чтениями/записями. У нас — простой блокирующий вариант.

### Обновление индексов на INSERT

```cpp
void Database::execute_insert(const Statement& st, std::ostream& out) {
    std::int64_t id = st.values[0];
    std::int64_t value = st.values[1];

    wal_->log_insert(id, value);
    tree_->insert(id, value);

    // Обновляем все secondary indexes.
    for (auto& kv : indexes_) {
        const std::string& col = kv.first;
        std::int64_t key = (col == col_names_[0]) ? id : value;
        kv.second.wal->log_insert(key, id);
        kv.second.tree->insert(key, id);
    }

    out << "1 row\n";
}
```

Каждый INSERT теперь делает **(1 + N) WAL fsync** для N индексов. На больших N это узкое место. Оптимизация в production — **group commit** (один fsync на batch операций) — упоминал в главе 35.

## Уникальность ключей в индексе

Подвох: secondary index должен мапить `value → id`. Что если две записи имеют **одинаковый value**?

Наш B+tree поддерживает **только уникальные ключи**. При повторной вставке того же ключа — старая запись перезаписывается.

В нашем демо мы вставляем `value` 100, 200, 300, 400, 500 — все уникальные. Работает. Если бы были дубликаты — индекс терял бы записи.

Решения в реальных СУБД:
- **Composite key**: ключ = `(value, id)` (пара). Все ключи уникальны.
- **Posting list**: значение — список ids (для дубликатов).
- **Duplicate-aware B+tree**: расширенные узлы хранят несколько values на одном key.

PostgreSQL использует composite keys для secondary indexes. SQLite — то же.

В нашей мини-СУБД для учебной цели — упрощение «уникальные значения». Реальный код требовал бы изменения B+tree или ключей.

## Query planner

Когда приходит `SELECT ... WHERE col op val`, нужно выбрать **стратегию выполнения**:

1. **PrimaryLookup**: `WHERE id = X`. Один `tree.find()`. O(log N), 1 IO + кэш.
2. **SecondaryLookup**: `WHERE col = X` с индексом на `col`. `idx.find()` → `tree.find()`. O(log N), 2 IO.
3. **SecondaryScan**: `WHERE col > X` (или другой range) с индексом. Range scan secondary + fetch каждой строки.
4. **FullScan**: ничего не подходит. Перебор всей основной таблицы с фильтром.

Простейший планировщик:

```cpp
std::string Database::plan_select(const Statement& st) const {
    if (!st.has_where) return "FullScan(primary)";
    if (st.where_col == col_names_[0] && st.where_op == "=") {
        return "PrimaryLookup(id=" + std::to_string(st.where_val) + ")";
    }
    auto it = indexes_.find(st.where_col);
    if (it != indexes_.end()) {
        if (st.where_op == "=") {
            return "SecondaryLookup(" + it->first + "=" + ... + ")";
        }
        return "SecondaryScan(" + ... + ")";
    }
    return "FullScan with filter (" + ... + ")";
}
```

Возвращает **строку** — описание плана. Используется в `EXPLAIN` для вывода, и в `execute_select` для диспетчеризации.

В настоящей СУБД план — **дерево операторов**: `Filter(IndexScan(idx_val, value > 250))`. Каждый узел — отдельный класс. Это позволяет составные планы (join'ы, агрегации). У нас — простая строка.

## Стратегии execute

```cpp
void Database::execute_select(const Statement& st, std::ostream& out) {
    std::string plan = plan_select(st);

    if (st.is_explain) {
        out << "Plan: " << plan << "\n";
        return;
    }

    // ... печать заголовка ...

    if (!st.has_where) {
        run_full_scan(...);
    } else if (st.where_col == col_names_[0] && st.where_op == "=") {
        run_primary_eq(...);
    } else if (auto it = indexes_.find(st.where_col); it != indexes_.end()) {
        run_secondary(..., it->second, ...);
    } else {
        run_full_scan(...);
    }
}
```

Те же четыре ветки, что в планировщике.

### run_primary_eq

```cpp
void Database::run_primary_eq(...) {
    std::int64_t v = 0;
    if (tree_->find(st.where_val, v)) {
        print_row(cols, st.where_val, v, out);
    } else {
        // не найдено
    }
}
```

Один `find`. Самый быстрый случай.

### run_secondary

```cpp
void Database::run_secondary(const Statement& st, ..., const SecondaryIndex& idx, ...) {
    auto emit = [&](std::int64_t row_id) {
        std::int64_t row_value = 0;
        if (!tree_->find(row_id, row_value)) return;
        print_row(cols, row_id, row_value);
    };

    if (st.where_op == "=") {
        std::int64_t row_id = 0;
        if (idx.tree->find(st.where_val, row_id)) emit(row_id);
    } else {
        // Range scan через secondary
        idx.tree->scan([&](std::int64_t key, std::int64_t row_id) {
            // Проверка предиката
            if (matches(...)) emit(row_id);
        });
    }
}
```

Для `=` — один `find` в индексе, потом `find` в основной таблице. Два дерева, **два** O(log N).

Для range — **scan** индекса (`scan` в нашем B+tree обходит весь leaf chain), для каждого подходящего — `find` в основной. Если индекс отсортирован по value, range scan по indexed-полю эффективен.

Реальная оптимизация: для range `value > 250` начать scan не с начала индекса, а с `find_leaf(250)`. Тогда обход — только подходящие записи. У нас scan начинается с самого левого листа — для большого диапазона неоптимально.

### EXPLAIN

```cpp
if (st.is_explain) {
    out << "Plan: " << plan << "\n";
    return;
}
```

Просто печатаем план, не выполняя. Это позволяет пользователю **проверить**, какой план выберет planner, перед запуском дорогого запроса.

## Демо

```sql
> CREATE TABLE items (id INT PRIMARY KEY, value INT);
> INSERT INTO items VALUES (1, 100);
> ... 5 строк ...

> EXPLAIN SELECT * FROM items WHERE id = 3;
Plan: PrimaryLookup(id=3)

> EXPLAIN SELECT * FROM items WHERE value = 300;
Plan: FullScan with filter (value = 300)         ← пока нет индекса

> EXPLAIN SELECT * FROM items WHERE value > 250;
Plan: FullScan with filter (value > 250)

> CREATE INDEX idx_val ON items(value);
Index 'idx_val' on value (5 rows indexed)

> EXPLAIN SELECT * FROM items WHERE value = 300;
Plan: SecondaryLookup(value=300)                  ← теперь через индекс!

> EXPLAIN SELECT * FROM items WHERE value > 250;
Plan: SecondaryScan(value > 250)

> SELECT * FROM items WHERE value = 300;
id       | value   
---------+---------
3        | 300     
(1 rows)

> INSERT INTO items VALUES (6, 600);             ← обновит индекс автоматически
> SELECT * FROM items WHERE value = 600;
6        | 600
```

Видно: **до** `CREATE INDEX` запросы по `value` идут через full scan. **После** — через index. EXPLAIN наглядно показывает разницу.

После `INSERT (6, 600)` индекс автоматически содержит `600 → 6`. `SELECT WHERE value = 600` находит через индекс.

## Сложные query planners

Production-планировщики делают **гораздо больше**.

### Cost-based optimization

Для каждого возможного плана **оценить стоимость** (cost estimation):
- Сколько строк прочитается.
- Сколько IO потребуется.
- Сколько CPU.

Выбрать план с **минимальной** оценочной стоимостью.

PostgreSQL хранит **статистику** о данных: `pg_statistic` — гистограммы значений, частоту дубликатов, и так далее. Используется для оценки.

Простой пример: `WHERE col = X`. Сколько строк подойдёт?
- Если `X` в гистограмме — оценка точная.
- Иначе — экстраполяция.

Для индексированного поиска — `est_rows × cost_per_index_lookup`. Для full scan — `total_rows × cost_per_seq_scan`. Выбираем меньшее.

В нашем простом планировщике — **rule-based**: всегда использовать индекс, если есть. Это **обычно** правильно, но не всегда. Если в таблице 100 строк, full scan может быть **быстрее** index lookup из-за overhead. Но мы упрощаем.

### Joins

Когда `SELECT ... FROM a JOIN b ON a.id = b.id`, планировщик выбирает **алгоритм join'а**:

- **Nested Loop**: для каждой строки a — поиск в b. O(N×M) без индекса.
- **Hash Join**: построить hash таблицу из меньшей, пройтись по большей. O(N+M).
- **Merge Join**: отсортировать обе по join-ключу, idti параллельно. O(N+M) если сортировка дешёвая (например, индексы уже отсортированы).

И **порядок** join'ов в multi-table запросе — отдельная NP-сложная задача.

У нас нет joins, поэтому ничего этого нет. Тема для отдельной книги.

### Composite indexes

Иногда полезен индекс на **две колонки**: `CREATE INDEX ON items(category, value)`. Тогда запрос `WHERE category = 'foo' AND value > 100` использует индекс эффективно.

Порядок в индексе **важен**: `(category, value)` хорош для `category = ... AND value ...`, не очень для `value = ...` (без category).

В нашей мини-СУБД таких индексов нет — поддерживаем только single-column.

### Index-only scan

Если в запросе **только** поля, попадающие в индекс, можно **не идти в основную таблицу**. Просто scan индекса — у которого, кстати, обычно меньше страниц = быстрее.

PostgreSQL делает это для запросов вида `SELECT value FROM items WHERE value > 250` — все поля видны в indexed B+tree, основную таблицу не трогаем.

В нашей версии — всегда `tree_->find(row_id)` за value. Простая реализация, можно ускорить.

## Главные правила главы

1. **Primary index = автоматически по pkey.** Secondary = добавляется через CREATE INDEX.
2. **Индексы стоят** места на диске и времени на запись. Не злоупотребляйте.
3. **Каждый INSERT обновляет все индексы.** Group commit для масштаба.
4. **Query planner выбирает стратегию** на основе доступных индексов.
5. **EXPLAIN** — обязательная команда. Без неё нет диагностики производительности.
6. **Cost-based** оптимизация на больших таблицах. Rule-based — для простых.
7. **Уникальность ключей** — простое ограничение в учебной СУБД. В production — composite keys или posting lists.
8. **Index-only scan** — оптимизация для запросов, у которых все поля в индексе.

## Маленькое упражнение

1. Запустите. Сравните EXPLAIN до и после `CREATE INDEX`.

2. Сделайте 10000 вставок (через цикл в коде). Засеките время. Создайте индекс. Засеките `SELECT WHERE value = X` до и после. Должно быть в сотни раз быстрее.

3. Добавьте команду `DROP INDEX idx_name` — удаление индекса.

4. Добавьте секцию «индексы» в EXPLAIN: `Plan: SecondaryScan(idx_val: ...)` с явным именем индекса.

5. (Сложнее) Поддержите дубликаты значений в secondary index через composite keys. Подсказка: ключ = `value × 1_000_000 + id` или `pack` в `int64`.

6. (Сложнее) Реализуйте **index-only scan**: если все select-колонки покрыты secondary index'ом — не идти в основную таблицу.

7. (Сложнее) Сделайте **cost-based planner**: оценивайте стоимость каждой стратегии и выбирайте минимальную. Учтите размер таблицы.

8. (Очень сложно) Добавьте **composite indexes**: `CREATE INDEX ON items(id, value)`. Парсер должен принимать несколько колонок. Индекс ключует пару, упорядочивает по первой колонке.

## Что дальше

Глава 38 — **REPL-клиент**. Сейчас наш main выполняет хардкод-список запросов. Сделаем интерактивный prompt: пользователь вводит SQL, видит результат, набирает следующий. Это уже **похоже на `psql`**.

Глава 39 — **бенчмарки и итог Части IV**. Замерим производительность нашей мини-СУБД на 1M вставках, сравним с SQLite, обсудим, что осталось «за кадром». Часть IV закроется.
