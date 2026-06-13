# Глава 25. Финал RPG

Часть II подходит к концу. У нас в репозитории `demo-rpg/` — больше 1600 строк C++, полностью рабочий мини-движок текстовой RPG. Игрок ходит по миру, поднимает предметы, использует расходники, сохраняет и загружает игру. Мир — в текстовом файле, можно править без перекомпиляции.

В этой финальной главе мы:
1. Добавим **юнит-тесты** для основных классов — практика, которой пользуются на любых проектах.
2. Соберём итог: что построили в C++, какие приёмы освоили.
3. Поговорим про сборку на других платформах.
4. Обсудим, что осталось «за кадром» и можно добавить в качестве упражнений.

После этой главы Часть II закрыта. Дальше — три новых проекта (mini-shell, мини-СУБД, TCP-чат) и обзорная Часть VI про C++17 как бонус.

## Зачем юнит-тесты

Программа из 1600 строк уже достаточно большая, чтобы случайные изменения ломали неожиданные вещи. Вы правите `Inventory::take`, а тут ломается `cmd_drop`. Юнит-тесты — **первая линия защиты** от регрессий.

Хороший тест:
- Покрывает один сценарий (одна функция, один аспект).
- Запускается за миллисекунды.
- Не зависит от других тестов.
- Чётко говорит, что прошло и что упало.

Юнит-тесты пишутся **на ту же кодовую базу**, ловят баги до того, как они доберутся до пользователя, и заодно служат документацией: «вот так этот класс используется».

## GoogleTest vs свой простой framework

Стандартный фреймворк тестирования C++ — **GoogleTest**. Он мощный (макросы `EXPECT_EQ`, фикстуры, параметризованные тесты), но требует подключения внешней зависимости.

Для нашего учебного проекта возьмём **свой минимальный test-runner**. Это:
- Никаких внешних зависимостей.
- Видна вся логика подсчёта.
- Достаточно для нашего масштаба.

В реальном проекте на нескольких десятках тысяч строк — GoogleTest или Catch2. Идея та же, просто фреймворк удобнее.

## Свой test-framework

`tests/test_framework.h`:

```cpp
#ifndef RPG_TEST_FRAMEWORK_H
#define RPG_TEST_FRAMEWORK_H

#include <iostream>
#include <sstream>
#include <string>

namespace test {

struct Stats {
    int total = 0;
    int passed = 0;

    int report(const std::string& suite) const {
        std::cout << "[" << suite << "] " << passed << "/" << total
                  << " прошло\n";
        return (passed == total) ? 0 : 1;
    }
};

}  // namespace test

#define CHECK(stats, cond)                                                  \
    do {                                                                    \
        (stats).total++;                                                    \
        if (cond) {                                                         \
            (stats).passed++;                                               \
        } else {                                                            \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__              \
                      << " — " #cond << "\n";                                \
        }                                                                   \
    } while (0)

#define CHECK_EQ(stats, a, b)                                               \
    do {                                                                    \
        (stats).total++;                                                    \
        auto&& _a = (a);                                                    \
        auto&& _b = (b);                                                    \
        if (_a == _b) {                                                     \
            (stats).passed++;                                               \
        } else {                                                            \
            std::ostringstream _ss;                                         \
            _ss << "FAIL " << __FILE__ << ":" << __LINE__                    \
                << " — " #a " == " #b " (got " << _a << " vs " << _b << ")"; \
            std::cerr << _ss.str() << "\n";                                  \
        }                                                                   \
    } while (0)

#endif
```

Разбор макросов.

**`do { ... } while (0)`** — стандартный приём, чтобы макрос вёл себя как одна инструкция. Без этого `if (cond) CHECK(...);` сломался бы — `do/while(0)` фиксирует «один блок, требующий `;` после».

**`#cond`** — **stringification**: имя выражения превращается в строку. `CHECK(s, x > 5)` напечатает `"x > 5"`.

**`__FILE__` и `__LINE__`** — макросы препроцессора, подставляют имя файла и номер строки.

**`auto&& _a = (a);`** — захват выражения в локальную ссылку, чтобы вычислить **один раз** (выражение могло иметь побочные эффекты — например, `++i`).

Это не идеальный фреймворк (нет фикстур, параметризации), но достаточный.

## Юнит-тесты модулей

`tests/test_units.cpp`. Каждая функция — отдельная группа тестов:

```cpp
void test_player(test::Stats& s) {
    Player p("Alice", 50);
    CHECK_EQ(s, p.name(), std::string("Alice"));
    CHECK_EQ(s, p.hp(), 50);
    CHECK(s, p.alive());

    p.take_damage(20);
    CHECK_EQ(s, p.hp(), 30);
    p.heal(100);             // не выше max
    CHECK_EQ(s, p.hp(), 50);
    p.take_damage(1000);      // не ниже 0
    CHECK_EQ(s, p.hp(), 0);
    CHECK(s, !p.alive());
}
```

Проверяем: конструктор работает, ограничения работают (heal не превысит max, damage не загонит в минус), `alive()` правильно меняется.

```cpp
void test_player_validation(test::Stats& s) {
    bool empty_caught = false;
    try { Player p("", 10); }
    catch (const ValidationError&) { empty_caught = true; }
    CHECK(s, empty_caught);

    bool neg_caught = false;
    try { Player p("ok", -1); }
    catch (const ValidationError&) { neg_caught = true; }
    CHECK(s, neg_caught);
}
```

Проверяем валидацию: пустое имя и отрицательный hp бросают исключение. Это **обратная сторона**: показываем, что баги (поломки) тоже работают как ожидаешь.

```cpp
void test_inventory(test::Stats& s) {
    Inventory inv;
    CHECK(s, inv.empty());

    inv.add(make_unique<Weapon>("меч", 5, 8));
    inv.add(make_unique<Armor>("шлем", 3, 4));
    CHECK_EQ(s, inv.items().size(), static_cast<std::size_t>(2));
    CHECK_EQ(s, inv.total_weight(), 8);

    auto taken = inv.take("меч");
    CHECK(s, static_cast<bool>(taken));
    CHECK_EQ(s, taken->name(), std::string("меч"));
    CHECK_EQ(s, inv.items().size(), static_cast<std::size_t>(1));

    auto miss = inv.take("щит");
    CHECK(s, !miss);

    inv.clear();
    CHECK(s, inv.empty());
}
```

Полный жизненный цикл инвентаря: пустой → добавили → проверили → взяли → проверили → попытались взять несуществующее → проверили → очистили.

`static_cast<std::size_t>(2)` — для подавления `-Wsign-compare`: `size()` возвращает `size_t` (unsigned), а `2` — это `int` (signed). С строгими флагами компилятор требует, чтобы оба были одного знака.

### Парсер и save/load

```cpp
void test_world_parser(test::Stats& s) {
    std::string source =
        "location start\n"
        "name Старт\n"
        "desc Тут начало.\n"
        "exit east finish\n"
        "item weapon кинжал 2 4\n"
        "\n"
        "location finish\n"
        "name Финиш\n"
        "desc Конец.\n";
    std::istringstream in(source);
    World w = parse_world(in);
    CHECK_EQ(s, w.size(), static_cast<std::size_t>(2));

    const Location* start = w.find("start");
    CHECK(s, start != nullptr);
    CHECK_EQ(s, start->name(), std::string("Старт"));
    CHECK_EQ(s, start->exit("east"), std::string("finish"));
    CHECK_EQ(s, start->items().items().size(), static_cast<std::size_t>(1));
}
```

Парсер тестируем **на строке**, не на файле. Так быстрее и тест не зависит от внешних файлов. `parse_world` принимает `std::istream&` — через `std::istringstream` подаём строку.

```cpp
void test_save_load_roundtrip(test::Stats& s) {
    World w;
    Location loc("home", "Дом", "Тут уютно.");
    w.add(std::move(loc));

    Player p("Hero", 100);
    p.take_damage(30);
    p.inventory().add(make_unique<Consumable>("яблоко", 1, 5));

    std::string path = "/tmp/rpg_test_save.sav";
    save::write(path, p, w, "home");

    // Восстанавливаем в другой Player/World
    Player p2("Other", 50);
    World w2;
    Location loc2("home", "Дом", "Тут уютно.");
    w2.add(std::move(loc2));
    std::string cur;
    save::read(path, p2, w2, cur);

    CHECK_EQ(s, p2.name(), std::string("Hero"));
    CHECK_EQ(s, p2.hp(), 70);
    CHECK_EQ(s, p2.max_hp(), 100);
    CHECK_EQ(s, cur, std::string("home"));
    CHECK_EQ(s, p2.inventory().items().size(), static_cast<std::size_t>(1));
}
```

**Roundtrip тест**: записали → прочли → проверили совпадение. Если что-то сломали в сериализации — тест упадёт.

### main и запуск

```cpp
int main() {
    test::Stats s;
    test_player(s);
    test_player_validation(s);
    test_inventory(s);
    test_location(s);
    test_world(s);
    test_world_parser(s);
    test_world_parser_errors(s);
    test_save_load_roundtrip(s);
    return s.report("unit-tests");
}
```

Все тесты передают одну и ту же `Stats`. В конце — отчёт.

Сборка и запуск:

```bash
$ make tests
$ ./build/tests/test_units
[unit-tests] 37/37 прошло
$ echo $?
0
```

37 проверок, все прошли. Код возврата 0.

Если запустить через CI (Continuous Integration) — например, GitHub Actions — этот тест автоматически запускается на каждый push. Любая регрессия будет видна.

## Сборка на нескольких платформах

`Makefile` мы заточили под GCC/Clang с Unix-инструментами. Это работает:
- **macOS** — clang/g++ из Xcode Command Line Tools (глава 6).
- **Linux** — gcc из `build-essential`.
- **Windows + WSL2** — Linux-окружение в Windows.

Что **не работает** на нативном Windows (без WSL):
- `mkdir -p` — на Windows это другое.
- `rm -rf` — на cmd.exe нет `rm`.
- Возможно проблемы с UTF-8 в исходниках (MSVC требует особых флагов).

Решений несколько:

**1. Использовать CMake** вместо Makefile. CMake генерирует Visual Studio проекты на Windows, Makefile на Unix, Ninja-файлы — везде.

```cmake
cmake_minimum_required(VERSION 3.10)
project(rpg CXX)
set(CMAKE_CXX_STANDARD 11)

add_executable(rpg
    src/main.cpp
    src/game.cpp
    src/commands.cpp
    src/item.cpp
    src/inventory.cpp
    src/location.cpp
    src/player.cpp
    src/world.cpp
    src/world_parser.cpp
    src/save_manager.cpp
)
target_include_directories(rpg PRIVATE include)
target_compile_options(rpg PRIVATE -Wall -Wextra)
```

Команда:
```bash
$ cmake -B build
$ cmake --build build
```

Получаете `build/rpg` независимо от платформы.

**2. WSL2 на Windows.** Простейший вариант: пользователь на Windows ставит WSL2, дальше всё как на Linux.

**3. Условные правила в Makefile.** Можно усложнить наш Makefile, чтобы он автоопределял ОС:

```makefile
ifeq ($(OS),Windows_NT)
    MKDIR := mkdir
    RM    := rmdir /S /Q
else
    MKDIR := mkdir -p
    RM    := rm -rf
endif
```

— и так далее. Возможно, но рассыпает простоту.

Для учебной книги — **простой Makefile + рекомендация WSL на Windows**. На реальном проекте перешли бы на CMake.

## Что мы построили — обзор

Структура `demo-rpg/`:

```
include/
├── commands.h       — реестр команд
├── errors.h         — иерархия исключений GameError/ValidationError/WorldError
├── event_bus.h      — шаблонная шина событий
├── events.h         — типы игровых событий
├── game.h           — главный класс
├── inventory.h      — инвентарь
├── item.h           — иерархия предметов (abstract Item + Weapon/Armor/Consumable)
├── location.h       — локация с выходами и предметами
├── player.h         — игрок
├── save_manager.h   — сохранение/загрузка
├── util.h           — make_unique-шим
├── world.h          — мир (коллекция локаций)
└── world_parser.h   — парсер мира из text-файла
src/                  — реализации (.cpp на каждый заголовок)
tests/                — юнит-тесты
data/
├── world.txt        — описание мира
└── saves/           — слоты сохранений
build/                — артефакты сборки (в .gitignore)
Makefile
```

13 заголовков, 11 реализаций, 6 тестовых файлов. Около 1700 строк C++.

## Что покрыли в C++

Список языковых концепций, которые мы применили **в этом одном проекте**:

### Часть I — основы:
- Переменные и типы (`int`, `std::string`, `bool`).
- Управляющие конструкции (`if/else`, `while`, range-based for).
- Функции с разными способами передачи параметров.
- Память на стеке и куче, RAII.
- Строки и потоки ввода-вывода.
- Контейнеры STL (`vector`, `unordered_map`).
- Алгоритмы (`std::find_if`, `std::sort`, `std::replace`).

### Часть II — продвинутое:
- **Классы и инкапсуляция** — `Player`, `Location`, `World`, `Inventory`, `Item`.
- **Инициализационные списки** в конструкторах.
- **`const`-correctness** — `const`-методы, `const T&` для чтения.
- **Наследование и полиморфизм** — иерархия `Item → Weapon/Armor/Consumable`.
- **Виртуальные методы и `override`/`final`**.
- **Виртуальный деструктор** для базы.
- **Чистые виртуальные функции** (`= 0`).
- **`= delete`** для запрета копирования.
- **Smart pointers** — `std::unique_ptr<Item>` в инвентаре.
- **Move-семантика** — `std::move` для передачи владения.
- **Шаблоны** — `EventBus<T>`, `make_unique<T>`.
- **Variadic templates + perfect forwarding** в `make_unique`.
- **Лямбды** с захватами в `CommandRegistry`.
- **`std::function`** для type-erased обработчиков.
- **Исключения** — `throw`/`try`/`catch`, своя иерархия.
- **Strong exception guarantee** в `save::read`.
- **Парсинг текстовых файлов** через `istringstream`.
- **Сериализация** объектов в файл и обратно.
- **Контрольные суммы** для проверки целостности.
- **`namespace`** для организации кода.

Этого хватит, чтобы вы могли читать почти любой C++-проект из открытых источников. Незнакомые куски будут — но «слов» в вашем словаре хватает.

## Что осталось «за кадром»

Игру можно расширять. Идеи как **самостоятельные упражнения**:

### Бой

В мире нет врагов. Добавьте:
- Класс `Enemy` с hp, damage, name.
- Команда `attack <name>` — игрок и враг наносят урон друг другу. Если враг умер — выпадает loot. Если игрок умер — game over.
- В `data/world.txt` поддержите `enemy <name> <hp> <damage>` в локации.

### NPC и диалоги

- Класс `NPC` с именем и репликами.
- Команда `talk <npc>` — выводит реплику.
- Расширенные диалоги через «деревья» (ID реплики → варианты ответов).

### Экипировка

- В `Player` поля `equipped_weapon`, `equipped_armor` (как сырые указатели, наблюдают за предметами в инвентаре).
- Команды `wield <item>`, `wear <item>`.
- Урон/защита влияют на бой.

### Квесты

- Класс `Quest` с условием и наградой.
- NPC даёт квест («принеси меч»).
- Игрок выполняет — получает награду.

### Множество персонажей

- Возможность создать нескольких игроков, сохранения у каждого свои.

### События с EventBus

- `EventBus<DamageEvent>` для логов всего урона.
- `EventBus<ItemPickedEvent>` для системы ачивок.
- Подписки регистрируются в начале игры.

### Графика (terminal)

- ncurses на macOS/Linux, PDCurses на Windows — псевдо-окна и цвета.
- Карта мира в виде ascii-таблицы.
- Цвета по типам предметов.

### Графика (графическая)

- SDL2 или SFML — спрайты, мышь, звук.
- Это выходит из «консольной книги», но возможно.

### Сетевая игра

- Несколько игроков в одном мире через TCP.
- Конечно, после Части V (TCP-чат) у вас будут навыки.

Я не реализую эти расширения в книге — слишком много. Но дизайн позволяет добавлять, не ломая существующее. Это и есть **расширяемая архитектура**.

## Если бы я писал это профессионально

Если этот RPG-движок надо было довести до production-качества:

1. **CMake** вместо Makefile.
2. **GoogleTest** или **Catch2** для тестов.
3. **GitHub Actions** для CI: проверка сборки на Linux/macOS/Windows, запуск тестов.
4. **clang-format** для единого стиля кода.
5. **clang-tidy** для статического анализа.
6. **AddressSanitizer + UBSanitizer** в режиме `asan` в Makefile.
7. **fuzzing** для парсеров (`libFuzzer`, AFL) — автоматическая подача мусорных строк, чтобы найти крэши.
8. **Coverage** — какой процент кода покрыт тестами.
9. **Документация** — Doxygen для API.
10. **Логирование** — `spdlog` или своё.

Эти штуки делают код **серьёзным**, но требуют дополнительной инфраструктуры. Для учебного — мы сделали разумный минимум.

## Итог Части II

С **главы 15 (дизайн)** до **главы 25 (финал)** мы построили рабочую программу. Не «hello world», не «упражнение из учебника», а **проект, в который не стыдно показать собеседнику**. С тестами, документацией в коде, разделением модулей, обработкой ошибок, версионированием формата сохранений.

Если вы шли по главам последовательно, к этому моменту у вас должно быть ощущение «я могу написать C++-программу». Не «зазубрил синтаксис», а **могу разложить задачу, выбрать структуру данных, написать класс с правильной семантикой, обработать ошибки**. Это и есть **навык**.

Дальше — три новых проекта в Частях III–V и обзорная Часть VI. Каждый проект — отдельная область C++:

- **mini-shell** (Часть III) — POSIX API: процессы, файловые дескрипторы, сигналы.
- **мини-СУБД** (Часть IV) — низкоуровневая работа с файлами, бинарные форматы, B+tree.
- **TCP-чат** (Часть V) — сокеты, многопоточность, реактор-цикл.
- **C++17 как бонус** (Часть VI) — `optional`, `variant`, `filesystem`, `string_view`.

Каждая Часть — отдельный демо-проект в своей папке `demo-shell/`, `demo-db/`, `demo-chat/`. Структура и стиль будут похожи на RPG: `include/`, `src/`, `tests/`, Makefile.

## Главные правила главы

1. **Юнит-тесты — первая линия защиты.** Не покрытие важно, а возможность быстро ловить регрессии.
2. **Каждый класс — отдельный тест-файл.** Не сваливайте всё в один.
3. **Roundtrip-тесты** (запиши + прочти + сравни) — золото для сериализации.
4. **CI запускает тесты автоматически.** Без этого тесты живут только пока их вспомнят.
5. **CMake для cross-platform.** Makefile — для учёбы.
6. **AddressSanitizer + UBSanitizer** в дебажной сборке.
7. **Расширяемая архитектура** = добавление фич не ломает существующее.
8. **Не зазубривайте, понимайте.** Цель — навык, а не словарь.

## Маленькое упражнение

1. Запустите `make tests && ./build/tests/test_units`. Все 37 прошли?

2. Поломайте одну строку в `Player::heal` (например, уберите ограничение по max_hp). Запустите тесты — увидите fail.

3. Добавьте тест для команды `cmd_take`: создайте `Game`, имитируйте ввод (это сложно для CLI; можно через `Game::cmd_take(...)` напрямую через friend или public метод для теста).

4. Установите CMake (`brew install cmake` или `apt install cmake`). Напишите `CMakeLists.txt` по примеру выше. Соберите через `cmake -B build && cmake --build build`.

5. Реализуйте класс `Enemy` (имя, hp, damage). Добавьте поддержку в `world.txt`: `enemy гоблин 10 3` — кладёт врага в локацию. В `cmd_look` показывайте врагов.

6. Реализуйте команду `attack <enemy>` — пошаговый бой. Используйте `dynamic_cast` для определения «оружия» в инвентаре игрока.

7. (Сложнее) Добавьте `EventBus<DamageEvent>` в `Game`. Подпишите логгер в `register_commands`. В команде `attack` публикуйте события — увидите, как декаплинг работает.

8. (Сложнее) Прогоните игру под **AddressSanitizer**:
   ```bash
   make CXXFLAGS="-std=c++11 -Wall -fsanitize=address -g"
   ```
   Если ASan находит проблему — это серьёзный сигнал. Чистая программа не должна ничего давать.

## Что дальше

**Часть III — mini-shell `demo-shell/`** (главы 26–31): свой bash-подобный shell с пайпами, редиректами, builtins, историей. Это другая половина C++: системное программирование через POSIX API. Узнаем `fork`, `exec`, `wait`, `pipe`, `dup2`, сигналы, terminal raw mode.

Затем — **Часть IV (главы 32–39)**: мини-СУБД с B+tree, страничным файлом, WAL, мини-SQL.

**Часть V (главы 40–47)** — TCP-чат с реактор-циклом, atomic, многопоточностью.

**Часть VI (главы 48–51)** — C++17 как бонус.

Всё это — впереди. До встречи в Части III.
