# Глава 39. Бенчмарки и финал Части IV

Часть IV приближается к концу. У нас в `demo-db/` — почти 2000 строк C++: страничный файл с LRU-кэшем, B+tree-индекс, WAL, мини-SQL, secondary indexes, query planner, REPL. Это **настоящая** СУБД, маленькая, но полная: ACID-A и -D, индексы, persistence.

В этой главе:
1. **Замерим** производительность на разных нагрузках.
2. Подведём **итог** Части IV — что освоили.
3. Обсудим, что **не реализовали** — куда расти.
4. **Сравним** идейно с SQLite, PostgreSQL.

## Бенчмарк

`utils/bench.cpp` запускает:
1. Создать таблицу.
2. **Insert N строк** через SQL.
3. **Checkpoint**.
4. **Find N строк по id** (primary key).
5. **Find 100 строк по value БЕЗ индекса** (full scan каждый раз).
6. **Create secondary index** на value.
7. **Find N строк по value через индекс**.

`make && ./build/bench 500` для 500 строк.

Запуск:

```
=== Бенчмарк мини-СУБД, N = 500 ===

Note: MAX_KEYS=4 в нашем B+tree (учебное), реальная СУБД 200+

Insert:                232.204 ms, 2153.28 ops/s
Checkpoint:            3.39606 ms

Find by id (B+tree):   89.4436 ms, 5590.11 ops/s
Find by value (full scan, 100 samples): 1556.61 ms, 64.242 ops/s
Build index:           230.755 ms
Find by value (index): 176.584 ms, 2831.52 ops/s

Ускорение find-by-value с индексом: 44.0758x
```

Числа на моём ноуте (Apple M3, debug-сборка). Что важно:

### Insert: 2.1k ops/s

Для каждой вставки:
- `wal.log_insert + fsync` — миллисекунда на NVMe.
- B+tree insert (split возможен).

`fsync` доминирует. Если бы делать **group commit** (см. главу 35), один fsync на 100 вставок дал бы ~100× ускорение.

Реальный SQLite: ~50k inserts/s **с WAL и без fsync на каждую** (синхронный режим NORMAL). С `synchronous = FULL` (как у нас) — несколько тысяч.

PostgreSQL: ~10-20k inserts/s по умолчанию, до 100k с группированием и SSD.

### Find by id: 5.6k ops/s

Через B+tree, O(log_4 N) ≈ 4-5 IO. С page cache всё в памяти, IO дешёвые. Не быстрее, потому что overhead парсинга SQL для каждого запроса — десятки микросекунд.

В SQLite prepared statement переиспользует план; запрос быстрее. У нас всё парсится заново.

### Find by value (без индекса): 64 ops/s

Full scan на 500 строк = 500 точек проверки. 64 ops/s × 500 точек = 32k операций сравнения в секунду. Не быстро — overhead `tree.scan()` (читать каждую leaf-страницу), плюс парсинг SQL.

### Find by value (с индексом): 2.8k ops/s

С индексом — `idx.tree.find()` + `tree.find()` = 2 поиска по B+tree. Меньше IO, ближе к find by id.

**Ускорение 44× на 500 строках**. На миллионе было бы тысячи×.

### Что замедляет нашу мини-СУБД

Несколько узких мест:

1. **MAX_KEYS = 4** — дерево неестественно глубокое. С 250 высота была бы 1-2.
2. **fsync на каждом INSERT** — без group commit не масштабируется.
3. **Парсинг SQL каждый раз** — нет prepared statements.
4. **Debug-сборка** — без `-O2`. С `-O2` всё в 2-5 раз быстрее.
5. **Маленький cache (64 страницы)** — на больших данных промахи.
6. **Одинаковая сериализация в WAL и B+tree** — каждое поле проходит memcpy 2-3 раза.

С исправлениями каждый из пунктов даёт улучшение. Это **обычный путь** оптимизации: профилируйте, найдите узкое место, исправьте, повторите.

### Сравнение с release-сборкой

```bash
$ make MODE=release
$ ./build/bench 1000
Insert:    ~200 ms, 5000 ops/s (вместо 2153)
Find:      ~50 ms, 20000 ops/s
```

`-O2` ускоряет ~2-3 раза. Inline-функции, удаление лишних копий, лучше vectorization.

## Что мы построили — обзор

`demo-db/` — 1800 строк C++ в 14 файлах:

```
include/
├── binary.h          — write/read u16/u32/u64/string в LE
├── btree.h           — B+tree
├── database.h        — фасад
├── page.h            — PAGE_SIZE, Page = vector<unsigned char>
├── page_manager.h    — страничный файл + LRU cache
├── sql.h             — Statement, parse_sql
├── util.h
└── wal.h             — Write-Ahead Log
src/
├── btree.cpp         — поиск, вставка, split (~240 строк)
├── database.cpp      — execute_create/insert/select + query planner (~250 строк)
├── main.cpp          — REPL (~120 строк)
├── page_manager.cpp  — LRU, pread/pwrite, fsync (~150 строк)
├── sql.cpp           — токенизатор + recursive descent parser (~180 строк)
└── wal.cpp           — append-only log + replay (~150 строк)
utils/
└── bench.cpp         — бенчмарк (~80 строк)
```

Возможности:
- **CREATE TABLE / CREATE INDEX / INSERT / SELECT / EXPLAIN**.
- **Primary key индекс** автоматически, **secondary indexes** по запросу.
- **Query planner** с 4 стратегиями.
- **Persistence** в файлы.
- **Durability** через WAL + fsync.
- **Recovery** после краха через replay.
- **Checkpoint** = sync + truncate WAL.
- **REPL** с multi-line, history, meta-командами.

Это **архитектура реальной СУБД** в миниатюре.

## Что НЕ реализовали

Реальная СУБД в десятки раз больше. Из нашего списка отсутствуют:

### Delete и Update

```sql
DELETE FROM items WHERE id = 1;
UPDATE items SET value = 999 WHERE id = 1;
```

DELETE в B+tree сложен — borrow или merge с соседом, рекурсивно вверх. UPDATE = find + modify in place. Парсер + executor расширяются легко; B+tree.remove — много кода.

### Транзакции

```sql
BEGIN;
INSERT ...;
INSERT ...;
COMMIT;
-- или ROLLBACK;
```

Группа операций — либо все применены, либо все откачены. Реализация:
- При BEGIN — отметить точку в WAL.
- При COMMIT — записать COMMIT-запись.
- При ROLLBACK — записать ROLLBACK + replay не применяет операции этой транзакции.

Уровни изоляции (READ COMMITTED, REPEATABLE READ, SERIALIZABLE) — отдельная тема.

### JOIN

```sql
SELECT * FROM orders o JOIN customers c ON o.cust_id = c.id;
```

Несколько таблиц + связь между. Алгоритмы: nested loop, hash join, merge join. **Самая сложная** часть SQL после optimizer.

### Aggregates

```sql
SELECT COUNT(*), AVG(value) FROM items;
SELECT category, SUM(value) FROM items GROUP BY category;
```

`COUNT`, `SUM`, `AVG`, `MIN`, `MAX` плюс `GROUP BY`. Парсер + специальный executor.

### Concurrent access

Несколько процессов/потоков одновременно. Требует:
- **Locking** на уровне строки/страницы.
- **MVCC** (multi-version concurrency control) — у каждой транзакции свой snapshot.
- **Deadlock detection**.

PostgreSQL, MySQL InnoDB — MVCC. SQLite — file-level locking.

### Schema persistence

Сейчас `table_name_`/`col_names_` живут в памяти. Запустили программу заново — потеряли. В реальной СУБД схема — в системной таблице (`pg_class` и т.п.) или в metadata-страницах нашего файла.

### Replication

Master-slave: запись на master, чтение со slave. Делается через **отправку WAL** на slave + replay там. PostgreSQL streaming replication, MySQL binlog.

### Triggers / Stored procedures

`CREATE TRIGGER ...`, `CREATE FUNCTION ...`. Свой язык программирования внутри SQL (PL/pgSQL в PostgreSQL, T-SQL в MS SQL). **Огромная** тема.

### Foreign keys, constraints

`REFERENCES other_table(id)`. Проверка на каждом INSERT/UPDATE/DELETE — целостность данных.

### Distributed

Шардинг между серверами, координатор запросов, конс-протоколы (Paxos, Raft). Эта область — **distributed databases**: CockroachDB, Spanner, Cassandra.

Каждое — отдельная книга-другая.

## Сравнение идейно: наш ↔ SQLite ↔ PostgreSQL

| Свойство | Наш | SQLite | PostgreSQL |
|----------|-----|--------|------------|
| Размер кода | 1.8k LOC | ~150k | ~1.5M |
| Языки | C++ | C | C |
| Storage | B+tree | B+tree | B+tree (Heap + B-tree) |
| Page size | 4 KB | 4 KB (default) | 8 KB |
| WAL | Logical, простой | Physical, WAL mode | Physical, full |
| Транзакции | Нет | Да, ACID | Да, ACID + MVCC |
| Параллелизм | Нет | File-level lock | MVCC |
| SQL features | CREATE/INSERT/SELECT | Полный SQL-92 | SQL-92 + расширения |
| Joins | Нет | Все алгоритмы | Все алгоритмы + planner |
| Replication | Нет | Нет | Streaming + logical |
| Use case | Учебный | Embedded, mobile | Production server |

SQLite — отличный пример «маленькой» СУБД, которую можно прочитать целиком за месяц. У нас её упрощённая версия.

PostgreSQL — индустриальный стандарт. Те же концепции, но в десятки раз больше деталей.

## Чему мы научились

Часть IV была про:

- **Низкоуровневую работу с файлами**: pread/pwrite, fsync, mmap (упоминание).
- **Бинарные форматы**: endianness, alignment, packed structures, explicit serialization.
- **Структуры данных на диске**: B+tree поверх страниц.
- **Durability и atomicity**: WAL, recovery, checkpoint, group commit.
- **Парсинг**: tokenizer + recursive descent + AST + executor.
- **Простой query optimization**: rule-based planner, EXPLAIN.
- **Архитектуру СУБД**: страницы → tree → WAL → SQL → REPL.

И **главное**: вы теперь знаете, **как устроена БД изнутри**. Когда напишете SQL-запрос в production, вы понимаете, что движок делает. Какие операции дорогие. Зачем нужны индексы. Почему `EXPLAIN` показывает план. Это знание ценнее, чем тысячи запросов на собеседовании.

## Главные правила всей Части IV

1. **Страницы фиксированного размера** — основа всех СУБД.
2. **B+tree** для упорядоченного индекса. Большой fan-out, мало IO.
3. **WAL** для durability — пиши сначала в журнал, fsync.
4. **Идемпотентные операции** позволяют replay при recovery.
5. **Indexes** — двойной меч: быстрое чтение, медленная запись.
6. **EXPLAIN** до запуска — поймайте плохой план до отправки в production.
7. **Бенчмарк рано** — не оптимизируйте на глаз, измеряйте.
8. **Простая архитектура — основа.** Сложности добавляются по мере роста.

## Маленькое упражнение

1. Соберите. Запустите бенчмарк на разных N: 100, 1000, 10000. Постройте таблицу. Где O(N), где O(log N)?

2. Запустите `make MODE=release` + бенчмарк. Сравните с debug.

3. Увеличьте `MAX_KEYS` в `btree.h` до 64, 256. Сравните производительность.

4. (Сложнее) Реализуйте DELETE и UPDATE на основе нашего парсера. Без B+tree.remove можно: для DELETE по pkey — поставить tombstone (специальный value), сканировать игнорируя; для UPDATE — overwrite (B+tree уже поддерживает).

5. (Сложнее) Реализуйте COUNT(*) — добавьте поддержку `SELECT COUNT(*) FROM ...`. Парсер должен распознать функцию.

6. (Сложнее) Замените fsync на каждой записи на **group commit**: накапливать N записей, fsync один раз. Замерьте, насколько быстрее.

7. (Очень сложно) Поддержите простые транзакции: BEGIN/COMMIT/ROLLBACK. При ROLLBACK откатить все изменения с последнего BEGIN через WAL.

8. (Очень сложно) Реализуйте `SELECT ... FROM A, B WHERE A.id = B.id` через nested-loop join.

## Часть IV закрыта

Что у нас в `demo-db/`:

- 1750 строк рабочего кода C++.
- Полнофункциональная мини-СУБД.
- Бенчмарк программа.
- REPL клиент.

Что освоили концептуально:

- POSIX file IO низкоуровневый.
- B+tree алгоритм с операциями insert/find/split.
- WAL архитектура durability/recovery.
- Query parser + planner + executor.

## Что дальше

**Часть V — TCP-чат** (главы 40-47): сетевое программирование. Сокеты, многопоточность, atomic, реактор-цикл `epoll`/`kqueue`, собственный бинарный протокол, многокомнатный чат с историей.

Это **четвёртый и последний** большой проект книги. После него — **Часть VI**: C++17 как бонус (`optional`, `variant`, `filesystem`, `string_view`, ranges/concepts/coroutines обзорно).

Конец книги виден. Осталось ~10 глав.
