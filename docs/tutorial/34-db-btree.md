# Глава 34. B+tree

Самая алгоритмически тяжёлая глава Части IV. **B+tree** — структура данных, лежащая в основе **любой** настоящей реляционной СУБД: PostgreSQL, MySQL InnoDB, SQLite, Oracle. Это упорядоченное дерево, оптимизированное под дисковое хранение.

К концу главы у нас работает B+tree поверх нашего `PageManager`. Можно вставлять `(int64 ключ → int64 значение)`, искать, обходить в порядке возрастания. Дерево живёт в файле — открыли программу, прочитали, всё работает.

## Зачем B+tree

Альтернативные структуры:

**`std::map`** (красно-чёрное дерево) — балансированное бинарное. O(log N) операции. **Но**: каждый узел — отдельная аллокация на куче. Узлов миллионы — миллионы малых аллокаций. Кэш-промахи. Нельзя удобно сохранить на диск.

**Hash table** (`std::unordered_map`). O(1) операции. **Но**: нет порядка. Нельзя сделать range query «все ключи от 100 до 200». Нет stability при росте.

**Sorted array**. Поиск O(log N). **Но**: вставка O(N) — сдвигать всё. На миллион записей — нереально.

**B+tree** — компромисс. Дерево, **но широкое и неглубокое**:
- Каждый узел вмещает много ключей (сотни на нашем 4 KB).
- Высота — `log_M(N)`, где M — fan-out. На миллион записей и fan-out 250 — высота ~3.
- На диске **узел = страница**. Одно IO — это переход на один уровень глубже.

При поиске миллиона ключей на 4 KB страницах: ~3 IO. На SSD — несколько миллисекунд. Bad: если бы хранили в `std::map` на диске — миллионы IO.

## B-tree vs B+tree

Из семейства «многозначных деревьев» популярны два:

**B-tree**: значения лежат **во всех узлах** (внутренних и листьях). При поиске можно остановиться в середине, если нашёл.

**B+tree**:
- **Внутренние узлы** хранят только **ключи и указатели** на детей. Никаких значений.
- **Листья** хранят **ключи и значения**.
- **Листья связаны** в список (next-pointer) — для эффективного range scan.

Преимущества B+tree:
- Внутренние узлы компактнее (нет значений) → больше fan-out → меньше высота.
- Range queries через linked-leaf — O(K) для K результатов.
- Все «найденные» данные на одной глубине — стабильное время доступа.

Минус: каждый ключ хранится **дважды** — в листе и (если разделитель) во внутренних узлах. На миллионах это лишняя память. Compromise приемлемый.

В нашей мини-СУБД — **B+tree**.

## Структура узла

Каждый узел занимает **одну страницу**. Layout:

```
[0..1]   u16 type (0=Internal, 1=Leaf)
[2..3]   u16 num_keys
[4..7]   u32 next_leaf (только для Leaf; 0 если последний)
[8..15]  зарезервировано
[16..]   keys: int64[num_keys]
[...]    values: int64[num_keys]   (если Leaf)
         или
         children: PageId[num_keys + 1]   (если Internal)
```

**Internal node** имеет `num_keys` ключей и `num_keys + 1` детей. Ключ `i` — это **граница** между детьми `i` и `i+1`:
- Дети `< keys[0]` идут в `children[0]`.
- Дети `>= keys[0] && < keys[1]` идут в `children[1]`.
- ...
- Дети `>= keys[last]` идут в `children[last + 1]`.

**Leaf node** имеет `num_keys` ключей и столько же значений. Плюс `next_leaf` — id следующего листа в порядке возрастания. Нулевой `next_leaf` = последний.

## MAX_KEYS — порядок

Сколько ключей помещается в одну страницу?

Грубо: 4096 - 16 (header) = 4080 байт на тело. Для leaf: `keys + values = 16 байт на пару`. Помещается `4080 / 16 = 255` пар. Для internal: `8 байт ключа + 4 байта PageId на «слот»`. Помещается `~ 340` ключей.

**Большой fan-out = неглубокое дерево**.

Для нашей **учебной** реализации я ставлю `MAX_KEYS = 4`. Почему? Чтобы splits происходили часто, на 10-20 вставках. Тогда видно, как дерево растёт.

В production надо выставлять `MAX_KEYS ≈ 250`. Меняется одна константа.

## Поиск

Алгоритм:

1. Начинаем с корня.
2. Если узел — Leaf: бинарный поиск ключа. Найден — возвращаем значение, не найден — false.
3. Если Internal: бинарный поиск *upper_bound* по ключам. Получаем индекс ребёнка. Спускаемся туда.

```cpp
PageId BPlusTree::find_leaf(std::int64_t key, std::vector<PageId>* path) const {
    PageId pid = root_page_id_;
    while (true) {
        NodeData n = read_node(pid);
        if (n.type == Leaf) return pid;
        if (path) path->push_back(pid);

        auto it = std::upper_bound(n.keys.begin(), n.keys.end(), key);
        std::size_t idx = static_cast<std::size_t>(it - n.keys.begin());
        pid = n.children[idx];
    }
}
```

`std::upper_bound` находит первое значение `>` key. Если все меньше — возвращает `end()`. Индекс этого position — индекс правильного ребёнка.

`path` — стек посещённых внутренних узлов. Нужен для вставки (split может потребовать вставку в parent).

`find` — это find_leaf + linear search в листе:

```cpp
bool BPlusTree::find(std::int64_t key, std::int64_t& out_value) const {
    PageId leaf_pid = find_leaf(key, nullptr);
    NodeData leaf = read_node(leaf_pid);
    auto it = std::lower_bound(leaf.keys.begin(), leaf.keys.end(), key);
    if (it == leaf.keys.end() || *it != key) return false;
    out_value = leaf.values[static_cast<std::size_t>(it - leaf.keys.begin())];
    return true;
}
```

`std::lower_bound` находит первое `>=` key. Если оно равно — ключ есть, возвращаем значение. Иначе — нет.

Сложность: O(log_M N) IO + O(log M) сравнений на узел. На N=1M, M=4 это `log_4(1M) ≈ 10` IO. С `M=250` — `~2.5` IO.

## Вставка

Это сложнее. Алгоритм:

1. **find_leaf** — спускаемся до листа, запоминая путь.
2. Вставляем (key, value) в лист, сортировка через `std::lower_bound`.
3. Если лист **переполнен** (`size > MAX_KEYS`) — **split**: разделяем на два.
4. Поднимаем **separator key** в parent.
5. Если parent тоже переполнен — split parent.
6. Если split добрался до корня — создаём новый корень.

```cpp
void BPlusTree::insert(std::int64_t key, std::int64_t value) {
    std::vector<PageId> path;
    PageId leaf_pid = find_leaf(key, &path);
    NodeData leaf = read_node(leaf_pid);

    auto it = std::lower_bound(leaf.keys.begin(), leaf.keys.end(), key);
    std::size_t idx = static_cast<std::size_t>(it - leaf.keys.begin());

    if (it != leaf.keys.end() && *it == key) {
        // Replace.
        leaf.values[idx] = value;
        write_node(leaf_pid, leaf);
        return;
    }

    leaf.keys.insert(leaf.keys.begin() + idx, key);
    leaf.values.insert(leaf.values.begin() + idx, value);

    if (leaf.keys.size() <= MAX_KEYS) {
        write_node(leaf_pid, leaf);
        return;
    }

    // Split.
    std::int64_t sep = 0;
    PageId right = split_leaf(leaf_pid, leaf, sep);
    insert_in_parent(path, sep, right);
}
```

Замена существующего ключа — простой write. Иначе вставляем в нужную позицию.

### Split leaf

```cpp
PageId BPlusTree::split_leaf(PageId leaf_pid, NodeData& leaf,
                             std::int64_t& sep_key) {
    std::size_t mid = leaf.keys.size() / 2;

    NodeData right;
    right.type = Leaf;
    right.keys.assign(leaf.keys.begin() + mid, leaf.keys.end());
    right.values.assign(leaf.values.begin() + mid, leaf.values.end());
    right.next_leaf = leaf.next_leaf;

    leaf.keys.resize(mid);
    leaf.values.resize(mid);

    PageId right_pid = pm_.allocate_page();
    leaf.next_leaf = right_pid;

    write_node(leaf_pid, leaf);
    write_node(right_pid, right);

    sep_key = right.keys.front();
    return right_pid;
}
```

Делим пополам. **Все ключи с правой половины** идут в новый правый лист. Левый сохраняет только первую половину.

`next_leaf` правого узла = старый next_leaf (сохраняем цепочку). `next_leaf` левого = id правого (вставляем правого в цепочку).

**Separator key** для parent — первый ключ правого листа. В B+tree separator **копируется** в parent (не **выносится** как в B-tree).

### Split internal

```cpp
PageId BPlusTree::split_internal(PageId int_pid, NodeData& node,
                                 std::int64_t& sep_key) {
    std::size_t mid = node.keys.size() / 2;
    sep_key = node.keys[mid];

    NodeData right;
    right.type = Internal;
    right.keys.assign(node.keys.begin() + mid + 1, node.keys.end());
    right.children.assign(node.children.begin() + mid + 1, node.children.end());

    node.keys.resize(mid);
    node.children.resize(mid + 1);

    PageId right_pid = pm_.allocate_page();
    write_node(int_pid, node);
    write_node(right_pid, right);
    return right_pid;
}
```

Для internal **средний ключ выносится в parent** (не копируется). Поэтому правый получает `keys.begin() + mid + 1`, не `+ mid`. Это особенность: в B+tree leaf splits **копирует** separator, а internal splits — **выносят**.

### Insert in parent

```cpp
void BPlusTree::insert_in_parent(const std::vector<PageId>& path,
                                 std::int64_t sep_key,
                                 PageId right_child) {
    if (path.empty()) {
        // Корень разбился — создаём новый корень.
        NodeData new_root;
        new_root.type = Internal;
        new_root.keys.push_back(sep_key);
        new_root.children.push_back(root_page_id_);
        new_root.children.push_back(right_child);
        PageId new_root_pid = pm_.allocate_page();
        write_node(new_root_pid, new_root);
        root_page_id_ = new_root_pid;
        save_metadata();
        return;
    }

    PageId parent_pid = path.back();
    NodeData parent = read_node(parent_pid);

    auto it = std::upper_bound(parent.keys.begin(), parent.keys.end(), sep_key);
    std::size_t idx = static_cast<std::size_t>(it - parent.keys.begin());
    parent.keys.insert(parent.keys.begin() + idx, sep_key);
    parent.children.insert(parent.children.begin() + idx + 1, right_child);

    if (parent.keys.size() <= MAX_KEYS) {
        write_node(parent_pid, parent);
        return;
    }

    std::int64_t new_sep = 0;
    PageId new_right = split_internal(parent_pid, parent, new_sep);
    std::vector<PageId> grand_path(path.begin(), path.end() - 1);
    insert_in_parent(grand_path, new_sep, new_right);
}
```

Если `path` пуст — это корень разбился. Создаём новый корень с двумя детьми (старый корень слева, новый правый справа). Сохраняем `root_page_id_`.

Иначе берём parent из path, вставляем (sep_key, right_child) в правильную позицию. Если parent не переполнен — записываем. Иначе **рекурсивно** разбиваем parent.

Это **самая хитрая** часть. Понять идею: корень — особенный (его split создаёт новый уровень дерева); остальные узлы — обычные (split добавляет ключ в parent).

## Range scan

`scan` обходит все пары в порядке возрастания. Используется **leaf chain**:

```cpp
void BPlusTree::scan(std::function<void(std::int64_t, std::int64_t)> cb) const {
    PageId pid = root_page_id_;
    while (true) {
        NodeData n = read_node(pid);
        if (n.type == Leaf) break;
        pid = n.children.front();
    }
    while (pid != 0) {
        NodeData leaf = read_node(pid);
        for (std::size_t i = 0; i < leaf.keys.size(); ++i) {
            cb(leaf.keys[i], leaf.values[i]);
        }
        pid = leaf.next_leaf;
    }
}
```

1. Спускаемся в самый левый лист (всё время `children.front()`).
2. Идём по `next_leaf` до конца.

Это **O(N)** для всего набора. Для range query (от ключа A до B) можно начать с `find_leaf(A)` и идти, пока ключ <= B. Будет O(log N + K) — оптимально.

## Удаление — кратко

Я не реализовал. Удаление — **намного** сложнее вставки:

1. Найти лист, удалить пару.
2. Если лист слишком пуст (< MAX_KEYS/2):
   - **Borrow** — взять ключ у соседа.
   - **Merge** — слить с соседом.
3. Если слили листы — у parent на одного ребёнка меньше.
4. Если parent теперь слишком пуст — recursive merge с его соседом.
5. Если merge добрался до корня — корень может «съёжиться».

Это десятки строк осторожного кода. SQLite не сразу научился правильному merge — было несколько раундов багфиксов в коде.

В нашей мини-СУБД insert+find хватит для демонстрации. Реальная СУБД обязательно делает и delete.

## Демо

```bash
$ ./build/mydb
=== mydb (глава 34: B+tree) ===

Вставлено 15 ключей.
Page count: 10
Root page:  9

Поиск:
  find(1) = 100
  find(5) = 500
  find(7) = 700
  find(12) = 1200
  find(99) = не найдено

Обход по порядку:
  1=100 2=200 3=300 4=400 5=500 6=600 7=700 8=800 9=900 10=1000 11=1100 12=1200 13=1300 14=1400 15=1500
```

15 ключей в случайном порядке (5, 1, 8, 3, ...) — после вставки `scan` выдаёт **отсортированный** список.

Дерево заняло 10 страниц = 40 KB. С MAX_KEYS=4 и 15 ключами получилось ~5 листьев + несколько внутренних. Корень — Page 9.

При `MAX_KEYS=250` — те же 15 ключей умесились бы в один лист, дерево из одной страницы.

## Дисковое дерево — что нового

Главное отличие от **в памяти**:

- **Нельзя держать всё в памяти.** Только узлы, которые сейчас обрабатываем (плюс через page cache).
- **`read_node`/`write_node`** — это IO. Минимизируем их количество.
- **`pm_.write_page` идёт через кэш**. На реальной СУБД важно, чтобы порядок записей был **правильный**: parent после child (или WAL гарантирует консистентность).
- **`pm_.sync()`** делает fsync. В нашем коде вызывается явно из main после `insert`.

При краше СУБД во время split — состояние может быть некорректным: например, новый правый лист записан, parent ещё нет. Решение — **WAL** (глава 35).

## Performance estimates

Для нашего MAX_KEYS=4 и 1M записей:
- Высота = `log_4(1M) ≈ 10`.
- Insert: 10 read + ~3 write (split chain). ~13 IO.
- Find: 10 read.

Для MAX_KEYS=250 и 1M:
- Высота = `log_250(1M) ≈ 2.5`.
- Insert: 3 read + 1 write среднее. Чаще без split.
- Find: 3 read. Если кэш горячий — 0 IO.

Реальные СУБД близки ко второму. Поиск ключа в индексе — **несколько микросекунд** на SSD с горячим кэшем.

## Тонкости

### Биение по середине

При splitting у нас `mid = size / 2`. Это упрощение. В реальной СУБД:
- Если все вставки **отсортированные** (1, 2, 3, ...) — split по середине даёт всегда полные левые и полупустые правые. Половина места теряется.
- Оптимизация: split со смещением 90/10 для append-only workload.

PostgreSQL имеет `fillfactor` — насколько заполнять страницу. По умолчанию 90% — оставляем 10% для удобства update.

### Concurrency

Наш B+tree — однопоточный. Параллельный доступ требует **locking**:
- Lock-coupling (latch crabbing): держим lock на parent пока проверяем child.
- Optimistic locking: пробуем читать без lock, если кто-то изменил — retry.
- Multi-version concurrency (MVCC) — каждая транзакция видит «свой» снимок дерева.

Это материал учебной книги по СУБД, не наша книга.

### Variable-length keys

У нас ключи `int64` — фиксированной длины. Реальные СУБД часто используют **строки переменной длины** как ключи.

Это сложнее: одна страница вмещает разное количество ключей в зависимости от их размера. Layout страницы становится дайнамичным. PostgreSQL делает это через **slotted pages**: массив указателей в начале + переменные данные в конце.

В нашем учебном — фиксированные ключи. Если будете расширять — изучите slotted-page-layout.

### Prefix compression

Для строковых ключей в B+tree часто применяют **prefix compression**: если все ключи в листе начинаются на «hello_», хранят префикс один раз. Экономия места в разы.

Опять же — оптимизация, не для нашей учебной.

## Главные правила главы

1. **B+tree = широкое неглубокое дерево**, оптимизированное под IO.
2. **Один узел = одна страница** (4 KB у нас).
3. **Большой fan-out** = меньше IO. На production: 200-500 ключей в узле.
4. **Internal: keys + children pointers.** Leaf: keys + values + next-pointer.
5. **Leaf chain** для эффективного range scan.
6. **Split при переполнении**, рекурсивно вверх. Корень может породить новый уровень.
7. **Separator key копируется при leaf split, выносится при internal split.** Особенность B+tree.
8. **Delete сложнее**; в production — обязательно, в учебном — можно опустить.

## Маленькое упражнение

1. Запустите `./build/mydb`. Изучите вывод — find и scan.

2. Увеличьте `MAX_KEYS = 50`. Соберите. Запустите. Сколько pages занимает? Какая root page?

3. Уменьшите `MAX_KEYS = 2`. Запустите. Какая высота дерева на 15 ключах?

4. Добавьте проверку: после каждого insert — `scan` показывает ключи в правильном порядке.

5. Реализуйте `range_scan(from, to, callback)` — обход ключей в диапазоне. Подсказка: `find_leaf(from)` плюс `next_leaf` пока ключ <= to.

6. (Сложнее) Реализуйте `count()` — общее число записей. Через scan — O(N). Можно ли быстрее? Подсказка: хранить счётчик в метаданных, обновлять при insert.

7. (Сложнее) Реализуйте **delete**. Borrow при < MAX_KEYS/2, merge если borrow не получился. Тестируйте на тысячах операций.

8. (Сложнее) Бенчмарк: 1M insert. Замерьте время. С MAX_KEYS=4 и MAX_KEYS=250 — сравните.

## Что дальше

Глава 35 — **WAL (Write-Ahead Log)**. Мы видели, что при splitting или другой составной операции краш может оставить структуру в плохом состоянии. WAL — это **журнал намерений**: «я собираюсь сделать X». Сначала пишем в WAL, потом делаем. После краша — повторяем по WAL то, что не доделали. Это и есть основа **транзакций**.

Дальше — мини-SQL (36), индексы (37), REPL (38), бенчмарки (39).
