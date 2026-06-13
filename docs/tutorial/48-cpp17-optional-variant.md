# Глава 48. C++17 — optional, variant, structured bindings, if constexpr

Начинается **Часть VI — C++17 как бонус**. До этого вся книга была на C++11. Четыре финальные главы — обзор того, что появилось в **C++14/17**, и как это упрощает код. Не отдельный проект, а **переписывание привычных паттернов** через современные средства.

Эта глава — четыре главные новинки **C++17**:
- **`std::optional<T>`** — «значение или нет».
- **`std::variant<T1, T2, ...>`** — typed union.
- **Structured bindings** — `auto [a, b] = ...`.
- **`if constexpr`** — ветвление на компиляции.

Все работают на компиляторе C++17 (gcc 7+, clang 5+). Если у вас C++11-only компилятор — главу читайте как «что есть в современном C++», без запуска.

## std::optional

Главная проблема старого C++: как функция возвращает «либо значение, либо ничего»?

Варианты до C++17:
1. **Special value** (-1 для int, "" для string). Опасно: что если -1 — реальный возраст?
2. **`bool find(key, T& out)`**. Out-параметр некрасивый.
3. **`std::pair<T, bool>`**. Лучше, но многословно.
4. **`T* find()`**. Указатель — но возможно nullptr-confusion.
5. **Throw exception**. Дорого для нормального «не нашли».

C++17: **`std::optional<T>`**.

```cpp
#include <optional>

std::optional<int> find_age(const std::string& name) {
    if (name == "Alice") return 30;
    return std::nullopt;
}

auto a = find_age("Alice");
if (a.has_value()) {
    std::cout << *a << "\n";   // 30
}

// Сокращение через bool:
if (auto b = find_age("Charlie")) {
    std::cout << *b;
}

// Дефолт:
int age = find_age("Charlie").value_or(0);   // 0

// Бросает bad_optional_access если пусто:
int x = find_age("ghost").value();   // throw
```

Что мы получили:
- **Тип сам говорит**: «может быть пусто». В сигнатуре `optional<int>`.
- **Никакого spec-value**. -1 теперь не нужен как «магия».
- **value_or, has_value** — удобные операции.

В `Inventory::find` из нашего RPG (глава 18) мы возвращали `const Item*` со значением nullptr для «не нашли». С `std::optional<Item*>` интерфейс был бы яснее. Но **указатели уже могут быть nullptr**, для них optional избыточен. `optional` нужен для **значимых типов** (int, double, string, struct).

### Когда optional хорош

- Возвращаемое значение, которого может не быть.
- Параметр функции, опциональный.
- Поле в struct, которое опционально (alternative — pointer).

### Когда optional плох

- Когда null имеет смысл «default». В JSON-парсинге `null` vs missing field — разные семантики; optional их объединяет, теряя различие.
- Когда есть **специальное значение** в домене (например, INT_MIN означает «уведомление»).

## std::variant

`std::variant<T1, T2, T3>` — tagged union. **Один из** N типов одновременно.

```cpp
#include <variant>

struct Weapon { int damage; };
struct Armor  { int defense; };
struct Potion { int heal_amount; };

using Item = std::variant<Weapon, Armor, Potion>;

Item it = Weapon{8};
```

`it` — это **значение** одного из трёх типов. Хранится **на стеке** (или в struct, где Item — поле). Размер = max(sizeof Ti) + tag (~1 байт).

Доступ:

```cpp
// 1. holds_alternative + get
if (std::holds_alternative<Weapon>(it)) {
    std::cout << std::get<Weapon>(it).damage;
}

// 2. std::visit — рекомендуется
std::visit([](const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, Weapon>) {
        std::cout << "Weapon, dmg=" << v.damage;
    } else if constexpr (std::is_same_v<T, Armor>) {
        std::cout << "Armor, def=" << v.defense;
    }
}, it);
```

`std::visit` берёт **callable** и применяет к текущему типу. Лямбда с `auto&` принимает любой тип; `if constexpr` диспатчит.

### variant vs наследование

В нашем RPG (главы 17-18) у нас была иерархия `Item → Weapon/Armor/Consumable` через виртуальные функции и `unique_ptr<Item>`.

С `variant`:
```cpp
using Item = std::variant<Weapon, Armor, Consumable>;
std::vector<Item> inventory;   // значения, не указатели на куче
```

Плюсы variant:
- **Значения на стеке/в vector** — нет аллокации, кэш-friendly.
- **Закрытое множество типов** — компилятор знает все. Может проверять exhaustiveness `visit`.
- **Без виртуальных функций** — меньше overhead, agresсивная инлайн.

Минусы:
- **Нельзя добавить новый тип** без правок везде, где `visit`. Иерархия с virtual — open для extension.
- **Тяжелее код visit'ов** в простых случаях.

**Когда что**:
- Полиморфизм с **открытым набором типов** (плагины, расширения) → наследование.
- **Закрытый набор**, известный заранее → variant.
- **AST-узлы**, **JSON-значения**, **состояния FSM** → variant идеален.

В нашем RPG — мог быть и variant. Если бы делали с нуля на C++17 — стоило бы подумать.

## std::any

`std::any` — «любой тип». Внутри type-erased значение.

```cpp
#include <any>

std::any x = 42;
x = std::string("hello");
x = 3.14;

if (x.type() == typeid(double)) {
    double d = std::any_cast<double>(x);
}
```

`any` хранит **любой** copyable тип. Тип проверяется в runtime через RTTI.

**Редко полезно**. Variant лучше когда множество типов закрытое. Any — когда **открытое и неизвестно заранее** (например, конфигурация с произвольными значениями, generic event bus).

Цена: аллокация в куче для больших типов + runtime overhead на cast'ы. **Используйте редко**.

## Structured bindings

Декомпозиция кортежей/пар/struct'ов в локальные имена.

До C++17:

```cpp
std::pair<std::string, int> p = ...;
std::string name = p.first;
int age = p.second;
```

C++17:

```cpp
auto [name, age] = p;
```

**Намного** короче. Особенно с tuple'ами:

```cpp
auto [id, name, height] = std::tuple{42, "Alice", 5.7};
```

### С map

```cpp
std::map<std::string, int> ages = {...};

// До C++17:
for (const auto& kv : ages) {
    std::cout << kv.first << " = " << kv.second;
}

// C++17:
for (const auto& [key, value] : ages) {
    std::cout << key << " = " << value;
}
```

Намного читабельнее. **Идиома** для C++17+.

### С `insert` / `emplace`

```cpp
std::map<std::string, int> m;
auto [it, inserted] = m.insert({"key", 42});
if (inserted) {
    std::cout << "новая запись\n";
}
```

`insert` возвращает `pair<iterator, bool>` — теперь раскладывается в два имени.

### Со struct

```cpp
struct Point { int x; int y; };
Point p{10, 20};

auto [x, y] = p;           // копии
auto& [rx, ry] = p;        // ссылки
const auto& [cx, cy] = p;  // const-ссылки
```

`auto&` — реально alias. Меняешь `rx` — меняешь `p.x`.

### Ограничения

- **Имена** фиксированы порядком полей, не их именами в struct. Если struct поменяется — все bindings перепишите.
- Для **`tuple` / `pair`** работает через `std::get<N>` — нужно правильное количество имён.
- В C++20 добавили **`auto& [_, value]`** для игнорирования (пока нет, в C++17 — все имена должны быть «настоящими»).

## if constexpr

Ветвление **на компиляции**, не в runtime.

До C++17 — **SFINAE**:

```cpp
template <typename T>
typename std::enable_if<std::is_integral<T>::value, void>::type
process(T x) {
    // только для integral
}

template <typename T>
typename std::enable_if<std::is_floating_point<T>::value, void>::type
process(T x) {
    // только для float
}
```

Уродливо. Каждый вариант — отдельная функция, разные сигнатуры с `enable_if`.

C++17 — **одна функция**:

```cpp
template <typename T>
void process(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integer: " << v * 2;
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "float: " << std::sqrt(v);
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "string size: " << v.size();
    } else {
        std::cout << "other";
    }
}
```

`if constexpr` **полностью убирает** «не выбранные» ветки **из** компиляции. Не «evaluates to false» — а вообще не компилируется.

Это важно: внутри ветки можно использовать **операции, недопустимые** для других типов:

```cpp
template <typename T>
void f(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        v % 2;   // не сработает для float, но if constexpr спасает
    }
}
```

Если бы был обычный `if` — `v % 2` пытался бы компилироваться для всех типов. Падало бы для float.

### typeid и type traits

`std::is_integral_v<T>`, `std::is_floating_point_v<T>`, `std::is_same_v<T, X>` — `_v` суффиксы от C++17, удобнее `::value`:

```cpp
// C++11
std::is_integral<T>::value

// C++17
std::is_integral_v<T>
```

Просто короче.

`std::decay_t<decltype(v)>` — убирает const/ref/&& с типа. В лямбда-callback'ах часто нужно.

## Inline variables

Мелочь, но полезная:

```cpp
// До C++17 — в .h:
extern const int MAX_SIZE;
// В .cpp:
const int MAX_SIZE = 100;

// C++17:
inline constexpr int MAX_SIZE = 100;
```

`inline` для переменной — разрешает определение в `.h` без `multiple definition`. Удобно для констант в header-only библиотеках.

## std::filesystem (упоминание)

Один из больших добавлений C++17 — `<filesystem>`. Кросс-платформенная работа с путями, файлами, каталогами.

Это **глава 49**, отдельно. Просто упомянем, что в C++17 теперь есть нормальное API.

## Когда переходить на C++17

В **новом проекте** на macOS/Linux/Windows — берите C++17 минимум, **сейчас уже C++20**. Старый код с C++11 — оставляйте; не переписывайте просто ради миграции.

В **учебной книге** мы остались на C++11 потому, что:
1. Всё работает на старых компиляторах.
2. C++17-фичи в основном **синтаксический сахар** — concepts остаются те же.

Когда **точно нужен C++17**:
- `std::filesystem` (вместо `<dirent.h>` или WinAPI).
- `std::optional` для чистых API.
- `std::variant` для AST/state machines.
- `if constexpr` для template-heavy код.

## Где переписать наш код

В нашей книге было бы естественно использовать:

### parser save (RPG, глава 24)

```cpp
// Сейчас:
Player p2("Other", 50);
save::read(path, p2, w2, current);

// С variant + optional:
struct SaveData {
    std::string name;
    int hp, max_hp;
    std::string current_loc;
    std::vector<std::variant<Weapon, Armor, Consumable>> items;
};

std::optional<SaveData> read_save(const std::string& path);
```

Чище: «нет данных» через `optional`, типы предметов через `variant`.

### B+tree (DB, глава 34)

`find` сейчас возвращает `bool` + out-param:

```cpp
bool find(int64_t key, int64_t& out_value);
```

С optional:

```cpp
std::optional<int64_t> find(int64_t key);
```

Использование: `if (auto v = tree.find(42)) { ... }`. Чище.

### parser SQL (DB, глава 36)

Statement сейчас — struct с kind-enum и «по умолчанию пустыми» полями. С variant:

```cpp
struct CreateTable { std::string name; std::vector<...> cols; };
struct Insert { std::string table; std::vector<int64_t> values; };
struct Select { std::string table; bool has_where; ... };

using Statement = std::variant<CreateTable, Insert, Select>;
```

Каждый тип — своя структура. `visit` диспатчит. Чище, типобезопаснее.

## Полное демо

`demo-cpp17/` содержит 4 файла:
- `optional_demo.cpp` — основы optional.
- `variant_demo.cpp` — Item-иерархия через variant.
- `structured_demo.cpp` — bindings везде.
- `if_constexpr_demo.cpp` — type-dispatched шаблон.

```bash
$ cd demo-cpp17
$ make
$ ./build/optional_demo
Alice: 30
Charlie не найден
Charlie age (or 0): 0
bad_optional_access поймали

$ ./build/variant_demo
=== std::visit + if constexpr ===
Weapon, dmg=8
Armor, def=4
Potion, heal=15
```

Каждый демонстрирует одну фичу автономно. Чтобы прочувствовать.

## Главные правила главы

1. **`std::optional<T>`** для «значение или нет» вместо магических спец-значений.
2. **`std::variant<...>`** для закрытого множества типов вместо иерархии virtual.
3. **`std::visit` + `if constexpr`** — идиоматический визитор для variant.
4. **`auto [a, b] = pair`** в range-for с map — стандарт C++17+.
5. **`if constexpr`** убирает SFINAE-ужас для type-dispatched шаблонов.
6. **`_v` / `_t` суффиксы** type traits — удобнее `::value` / `::type`.
7. **`inline constexpr`** для header-only констант.
8. **C++17 — реальный default** для нового кода в 2026.

## Маленькое упражнение

1. Соберите и запустите все 4 demo. Изучите вывод.

2. Перепишите функцию `World::find` из главы 16 чтобы возвращала `std::optional<const Location*>` (или просто `Location*`, но через variant с типом ErrorReason для других ошибок).

3. Замените в `BPlusTree::find` (глава 34) сигнатуру с out-param на `std::optional<int64_t>`. Сравните читаемость.

4. (Сложнее) Перепишите `Statement` (глава 36) на variant из CreateTable/Insert/Select. Executor через `std::visit` + `if constexpr`.

5. (Сложнее) Перепишите `Item`-иерархию (главы 17-18) на variant. Сравните производительность (1M insert/lookup).

6. Используйте `[[nodiscard]]` (C++17) для функций, чей возвращаемый результат **не должен игнорироваться**. Например, `[[nodiscard]] std::optional<int> find(...)`.

7. Прочитайте `std::optional::and_then` / `transform` (C++23). Monadic interface.

8. Изучите `std::variant<T...>::index()` — возвращает индекс активного варианта (0-based).

## Что дальше

Глава 49 — **std::filesystem**. Кросс-платформенный API для работы с файлами/папками. Зачем нужен, как заменить наш ручной `mkdir`/`open` в RPG и DB-проектах.

Дальше — string_view + parallel algorithms (50), куда расти после книги — обзор C++20/23 (51). Финал близок.
