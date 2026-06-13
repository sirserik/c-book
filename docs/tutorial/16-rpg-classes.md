# Глава 16. Классы — Player, Location, World

В прошлой главе мы собрали скелет проекта. Класс `Game` там уже был, но мы прошли по нему по верхам. Эта глава — глубокий разбор **классов** в C++ на примере трёх настоящих сущностей игры: `Player`, `Location`, `World`. К концу главы у игрока будут параметры (HP, имя), в мире — три локации, и можно ходить из одной в другую.

Главы 16-19 — самые «тяжёлые» в Части II. Это про объектно-ориентированный C++: классы, наследование, виртуальные функции, smart pointers, move-семантика. Если эти главы пройдут плотно — дальше будет легко.

## struct и class

В C++ есть два ключевых слова для определения пользовательских типов: `struct` и `class`. Технически они почти не отличаются. Единственная разница:

- В `struct` поля и методы по умолчанию **публичные**.
- В `class` поля и методы по умолчанию **приватные**.

```cpp
struct Point {
    int x;
    int y;
};

class Person {
    std::string name;   // приватное по умолчанию
    int age;            // тоже приватное
public:
    Person(std::string n, int a) : name(std::move(n)), age(a) {}
};
```

Конвенция:
- **`struct`** — для простых наборов данных без поведения. `Point`, `Color`, `Vector3`, `DivResult`. Поля торчат наружу, никакой инкапсуляции.
- **`class`** — для всего с инкапсуляцией, инвариантами, нетривиальной логикой.

Эту конвенцию никто не запрещает нарушать, но я в книге придерживаюсь. И в стандартной библиотеке так же: `std::pair` — `struct`, `std::vector` — `class`.

## Простой класс Player

Начнём с `Player`. У игрока должны быть:
- Имя.
- Текущее здоровье (`hp`).
- Максимальное здоровье (`max_hp`).
- Методы: получить урон, вылечиться, проверить, жив ли.

### Объявление

`include/player.h`:

```cpp
#ifndef RPG_PLAYER_H
#define RPG_PLAYER_H

#include <string>

namespace rpg {

class Player {
public:
    Player(std::string name, int max_hp);

    const std::string& name() const;
    int hp() const;
    int max_hp() const;
    bool alive() const;

    void take_damage(int amount);
    void heal(int amount);

private:
    std::string name_;
    int max_hp_;
    int hp_;
};

}  // namespace rpg

#endif
```

Разберём по элементам.

### Поля

```cpp
private:
    std::string name_;
    int max_hp_;
    int hp_;
```

Это **поля** (fields) — данные класса. У каждого объекта `Player` будут свои `name_`, `max_hp_`, `hp_`.

**Соглашение об именовании**: поля заканчиваются на подчёркивание. Это популярная конвенция (Google C++ Style Guide и многие другие). Зачем? Чтобы внутри метода легко различить:

```cpp
void set_name(const std::string& name) {
    name_ = name;    // присвоить полю значение параметра
}
```

Без подчёркивания пришлось бы либо использовать `this->name = name;`, либо переименовать параметр. Подчёркивание убирает путаницу.

Поля **приватные**: снаружи к `name_` напрямую обратиться нельзя. Это **инкапсуляция** — главная идея ООП. Класс отвечает за свои данные, никто извне их случайно не испортит.

### Методы доступа (getters)

```cpp
public:
    const std::string& name() const;
    int hp() const;
    int max_hp() const;
    bool alive() const;
```

Это **getters**. Они возвращают значение полей, никак их не меняя. Особенности:

`const std::string& name() const` — два `const` в одной строке, и они **разные**.

- Первый `const` — у возвращаемого типа: «возвращаю ссылку на неизменяемую строку». Так читатель не сможет через возвращённую ссылку поменять `name_`.
- Второй `const` (в конце) — это **`const`-метод**: «обещаю, что метод не меняет состояние объекта». Внутри `name()` нельзя написать `name_ = ...` — компилятор не позволит.

`const`-методы можно вызывать на **константных объектах**:

```cpp
const Player p("Hero", 100);
std::cout << p.name();      // OK: name() это const-метод
p.take_damage(5);            // ОШИБКА: take_damage не const
```

Это важно. Если у вас в коде где-то `const Player& p` (как параметр функции), на нём можно вызывать только `const`-методы.

**Правило**: **все методы, не меняющие объект, помечайте `const`**. Это документация для читателя и контракт для компилятора. Привычка `const`-correctness — отличает уверенного C++-программиста.

### Модифицирующие методы

```cpp
public:
    void take_damage(int amount);
    void heal(int amount);
```

Эти меняют состояние — не `const`.

### Конструктор

```cpp
public:
    Player(std::string name, int max_hp);
```

**Конструктор** — особый метод, имя которого совпадает с именем класса. Возвращаемого типа нет (даже `void` не пишут).

Вызывается **автоматически** при создании объекта:

```cpp
Player hero("Alice", 100);
// неявно вызывается Player::Player("Alice", 100)
```

Параметры конструктора `name` (по значению `std::string`) и `max_hp` (по значению `int`) — это входные данные для инициализации.

### Определение

`src/player.cpp`:

```cpp
#include "player.h"

#include <utility>

namespace rpg {

Player::Player(std::string name, int max_hp)
    : name_(std::move(name)),
      max_hp_(max_hp),
      hp_(max_hp) {
}

const std::string& Player::name() const {
    return name_;
}

int Player::hp() const {
    return hp_;
}

int Player::max_hp() const {
    return max_hp_;
}

bool Player::alive() const {
    return hp_ > 0;
}

void Player::take_damage(int amount) {
    if (amount < 0) amount = 0;
    hp_ -= amount;
    if (hp_ < 0) hp_ = 0;
}

void Player::heal(int amount) {
    if (amount < 0) amount = 0;
    hp_ += amount;
    if (hp_ > max_hp_) hp_ = max_hp_;
}

}  // namespace rpg
```

Каждый метод определяется как `ClassName::method`. Это правильное использование квалифицированного имени.

#### Инициализационный список — самое важное

Конструктор содержит **инициализационный список** после двоеточия:

```cpp
Player::Player(std::string name, int max_hp)
    : name_(std::move(name)),
      max_hp_(max_hp),
      hp_(max_hp) {
}
```

Каждое поле инициализируется в скобках. Это **правильный** способ. Альтернатива через присваивание в теле:

```cpp
Player::Player(std::string name, int max_hp) {
    name_ = name;
    max_hp_ = max_hp;
    hp_ = max_hp;
}
```

— работает, но **хуже**. Что происходит при таком стиле:

1. Сначала каждое поле создаётся **по умолчанию**. `name_` становится пустой строкой, `max_hp_` и `hp_` — неинициализированными.
2. Потом в теле конструктора им **присваиваются** нужные значения. То есть `std::string::operator=` вызывается, что лишняя работа.

С инициализационным списком — поля сразу создаются с правильными значениями, без промежуточного «пустого» состояния. Это **быстрее и правильнее**.

Для `int` разница невелика. Для `std::string` или собственных классов с дорогим конструктором — заметная.

И есть случаи, когда инициализационный список **обязателен**:
- Поля типа `const` (нельзя присвоить — только инициализировать).
- Поля-ссылки (то же самое).
- Поля без default-конструктора (классы, у которых нет конструктора без параметров).

Поэтому: **всегда инициализируйте поля в инициализационном списке, не в теле**.

Порядок в списке должен **совпадать с порядком объявления полей**. Если в классе:

```cpp
private:
    std::string name_;
    int max_hp_;
    int hp_;
```

Инициализация **именно** в этом порядке. Иначе компилятор молча перенаправит (поля всё равно инициализируются в порядке объявления), и `-Wreorder` это поймает.

#### std::move в конструкторе

Почему `name_(std::move(name))`?

В прошлой главе видел такое выражение, не разбирали детально. Объясню кратко (полностью — в главе 19).

`std::move(name)` — это «преобразование к rvalue». Грубо: «эту переменную я больше использовать не буду, можно её ограбить».

Без `std::move`:

```cpp
Player::Player(std::string name, int max_hp)
    : name_(name),       // КОПИЯ строки name
      ...
```

`name_(name)` — это **копия** строки. Аллокация на куче, копирование байтов. Если переданная строка большая — дорого.

С `std::move`:

```cpp
    : name_(std::move(name)),
```

`name_(std::move(name))` — **перемещение**: `name_` забирает себе внутренний указатель и буфер `name`. Никакой копии. Источник (`name`) становится «пустым», но это нормально — мы её больше не используем.

В новом C++-коде вы будете часто видеть `std::move` в конструкторах для строк и других «дорогих» типов. Это идиома.

#### Тело конструктора

После инициализационного списка идёт обычное тело `{ }`. Здесь можно делать дополнительную работу — выводы, проверки, что угодно. Часто оно пустое.

В нашем `Player::Player` — пусто. Все поля инициализированы списком.

### Что такое `this`

Внутри методов класса есть невидимый параметр **`this`** — указатель на текущий объект. Когда вы пишете внутри метода `hp_`, на самом деле подразумевается `this->hp_`.

```cpp
int Player::hp() const {
    return hp_;        // эквивалент: return this->hp_;
}
```

Часто `this->` опускают для краткости (поэтому подчёркивание в имени поля помогает не путать с параметрами). Но иногда нужен явный `this`:

```cpp
void Player::set_hp(int hp) {
    this->hp_ = hp;    // явно, чтобы не путать с параметром
}
```

Или когда нужно вернуть указатель/ссылку на сам объект:

```cpp
Player& Player::heal(int amount) {
    hp_ += amount;
    if (hp_ > max_hp_) hp_ = max_hp_;
    return *this;       // вернуть ссылку на сам объект
}
```

Это позволяет цепочки: `player.heal(5).heal(10).heal(15)` (правда, в нашем коде мы возвращаем `void`).

### Использование Player

В `Game::Game()` мы теперь создаём игрока:

```cpp
Game::Game()
    : running_(true),
      world_(make_demo_world()),
      player_("Герой", 20),
      current_location_id_("glade") {
}
```

`player_("Герой", 20)` — вызов конструктора `Player`. Это и есть инициализационный список самого `Game`.

В `Game::run()`:

```cpp
std::cout << "Привет, " << player_.name() << "!\n";
```

`player_.name()` — вызов const-метода. Возвращает `const std::string&`. Дальше `<<` для потока умеет работать со строкой.

## Локация: класс посложнее

`Location` хранит:
- ID (короткая строка-идентификатор).
- Имя (для отображения).
- Описание.
- Выходы — словарь «направление → ID соседней локации».

### Объявление

`include/location.h`:

```cpp
#ifndef RPG_LOCATION_H
#define RPG_LOCATION_H

#include <string>
#include <unordered_map>

namespace rpg {

class Location {
public:
    Location(std::string id, std::string name, std::string description);

    const std::string& id() const;
    const std::string& name() const;
    const std::string& description() const;

    void add_exit(const std::string& direction, const std::string& target_id);
    std::string exit(const std::string& direction) const;

    const std::unordered_map<std::string, std::string>& exits() const;

private:
    std::string id_;
    std::string name_;
    std::string description_;
    std::unordered_map<std::string, std::string> exits_;
};

}  // namespace rpg

#endif
```

Очень похоже на `Player`, только полей побольше.

### Определение

```cpp
Location::Location(std::string id, std::string name, std::string description)
    : id_(std::move(id)),
      name_(std::move(name)),
      description_(std::move(description)) {
}
```

Конструктор инициализирует строки через `std::move`. `exits_` не упомянут — он автоматически создаётся пустым `unordered_map`-ом (default constructor).

```cpp
void Location::add_exit(const std::string& direction, const std::string& target_id) {
    exits_[direction] = target_id;
}

std::string Location::exit(const std::string& direction) const {
    auto it = exits_.find(direction);
    if (it == exits_.end()) return {};
    return it->second;
}
```

`add_exit` — обычный setter. `exit` — поиск с возвратом пустой строки при отсутствии. Я предпочитаю возвращать пустую строку, а не выкидывать исключение — для нашей логики (не каждый ход надо проверять направления) это удобнее.

`return {};` — пустая инициализация для `std::string`. Эквивалент `return std::string();`.

Замечание: метод **называется `exit`**, но это не выход из программы. У нас в `namespace rpg`, имя не конфликтует с системным `exit()`. Так можно.

### Использование

```cpp
Location glade("glade", "Лесная поляна",
               "Залитая солнцем поляна. На западе тропа к деревне.");
glade.add_exit("west", "village");
glade.add_exit("east", "forest");
```

Создаём, заполняем выходами. Простой и понятный код.

## Перегрузка операторов — короткий взгляд

В стандартной библиотеке вы видели:

```cpp
std::string a = "hello";
std::string b = a + " world";       // оператор +
std::cout << a;                       // оператор <<
if (a == "hello") ...                 // оператор ==
```

Откуда эти операторы для `std::string`? Они **перегружены** в самом классе. C++ позволяет определить операторы для своих типов.

Подробный разбор — глава 20 (там вместе с шаблонами). Сейчас для контекста:

```cpp
class Vector2 {
public:
    Vector2(double x, double y) : x_(x), y_(y) {}
    
    Vector2 operator+(const Vector2& other) const {
        return Vector2(x_ + other.x_, y_ + other.y_);
    }

private:
    double x_, y_;
};

int main() {
    Vector2 a(1, 2);
    Vector2 b(3, 4);
    Vector2 c = a + b;     // вызов a.operator+(b), даёт {4, 6}
}
```

В нашей игре операторов мы пока не делаем — обходимся методами `add_exit`, `exit`. Но потом, в иерархии предметов и в сохранениях, будем перегружать `<<` для потоков (как `std::cout << player`).

## Конструктор по умолчанию

Если вы не пишете **никакого** конструктора, компилятор создаёт **конструктор по умолчанию** — без параметров. Он инициализирует поля их default-значениями.

```cpp
class Empty {
    int x;
    std::string y;
};

Empty e;       // OK: x неинициализирован (мусор), y пустая строка
```

Но как только вы написали **любой** свой конструктор — компилятор перестаёт автоматически создавать default. Поэтому:

```cpp
class Player {
public:
    Player(std::string name, int max_hp);   // мы написали свой
};

Player p;   // ОШИБКА: нет конструктора без параметров
```

Если вам нужен ещё и default:

```cpp
class Player {
public:
    Player();   // отдельно объявляем
    Player(std::string name, int max_hp);
};

Player::Player() : Player("Unnamed", 100) {}
// делегирование — вызов другого конструктора (C++11)
```

В нашем `Player` default-конструктор не нужен. В `World` — нужен:

```cpp
class World {
public:
    World();   // явно объявляем
    // ...
};

World::World() = default;
```

`= default` — особый синтаксис: «компилятор, создай мне дефолтную реализацию». Полезно, когда хочется явно показать, что default-конструктор есть, но писать его руками нечего.

### `= delete`

Антипод `= default`. Запрещает конструктор/метод:

```cpp
class Singleton {
public:
    Singleton(const Singleton&) = delete;            // запрет копирования
    Singleton& operator=(const Singleton&) = delete; // запрет присваивания
};
```

Если кто-то попытается скопировать `Singleton` — ошибка компиляции. Полезно для классов, которые не должны копироваться (RAII-обёртки для уникальных ресурсов).

## Копирование: rule of three

Что произойдёт, если мы скопируем объект?

```cpp
Player a("Hero", 100);
Player b = a;       // копия
```

Компилятор автоматически генерирует **конструктор копирования**:

```cpp
Player(const Player& other)
    : name_(other.name_),
      max_hp_(other.max_hp_),
      hp_(other.hp_) {
}
```

Поле за полем. Для встроенных типов и `std::string` это работает корректно (`std::string` сам знает, как себя копировать).

Также генерируется **оператор присваивания при копировании**:

```cpp
Player& operator=(const Player& other) {
    name_ = other.name_;
    max_hp_ = other.max_hp_;
    hp_ = other.hp_;
    return *this;
}
```

И **деструктор** (для `Player` он тривиальный — встроенные типы и `std::string` сами уничтожаются).

Это **«rule of zero»** (правило нуля): если ваш класс хранит только стандартные типы и контейнеры — **ничего не писать**, компилятор всё сгенерирует правильно.

### Когда rule of zero ломается

Если поле — **сырой указатель** на динамическую память:

```cpp
class IntHolder {
public:
    IntHolder(int v) : ptr_(new int(v)) {}
    ~IntHolder() { delete ptr_; }   // вот, освобождаем

private:
    int* ptr_;
};

IntHolder a(42);
IntHolder b = a;          // !!! компилятор скопирует ptr_ как есть
// теперь у a и b ОДИН указатель на тот же int
// при уничтожении обоих — двойной delete, UB!
```

Это типичный баг. Компилятор копирует **по умолчанию** поверхностно (shallow copy) — то есть копирует значения полей. Для указателя значение — адрес. Глубокого копирования (создать новый объект и скопировать содержимое) не делает.

Решение — **«rule of three»**: если у класса есть **нетривиальный деструктор** (нужен для управления ресурсом), скорее всего нужны и:
- Конструктор копирования (правильно копировать ресурс).
- Оператор присваивания (то же).

```cpp
class IntHolder {
public:
    IntHolder(int v) : ptr_(new int(v)) {}
    
    ~IntHolder() { delete ptr_; }
    
    IntHolder(const IntHolder& other) : ptr_(new int(*other.ptr_)) {}
    
    IntHolder& operator=(const IntHolder& other) {
        if (this != &other) {              // защита от self-assignment
            int* new_ptr = new int(*other.ptr_);
            delete ptr_;
            ptr_ = new_ptr;
        }
        return *this;
    }

private:
    int* ptr_;
};
```

Целая программа из-за одного указателя. Поэтому в современном C++ **сырые указатели почти не используются** — берут `std::unique_ptr` (глава 18), у которого правильное копирование/перемещение из коробки.

### Rule of five (C++11)

В C++11 добавились **move-семантика**: перемещение объекта без копирования. Это потребовало двух новых специальных функций:
- Конструктор перемещения.
- Оператор присваивания при перемещении.

Если у класса нетривиальный деструктор, скорее всего нужны **пять** функций — это **rule of five**:

1. Деструктор.
2. Конструктор копирования.
3. Оператор присваивания при копировании.
4. Конструктор перемещения (новинка).
5. Оператор присваивания при перемещении (новинка).

Подробно про move — в главе 19. Сейчас знайте, что в современном C++:

- **Rule of zero** — лучший выбор: всё стандартно, ничего писать не надо.
- **Rule of five** — если нужно управлять ресурсами руками.
- **Rule of three** — устаревшее, для C++03.

### Запрет копирования

Иногда копирование нежелательно (например, наш `LogFile_BAD` из главы 11). Запрещаем явно:

```cpp
class LogFile {
public:
    LogFile(const std::string& filename);
    ~LogFile();
    
    LogFile(const LogFile&) = delete;            // запрет копирования
    LogFile& operator=(const LogFile&) = delete;
};
```

Это удобно для RAII-классов: копировать файл (значит, иметь два владельца одного дескриптора) — глупость.

## World: контейнер локаций

`World` хранит коллекцию `Location`-ов и предоставляет доступ по ID.

### Объявление

`include/world.h`:

```cpp
#ifndef RPG_WORLD_H
#define RPG_WORLD_H

#include "location.h"

#include <string>
#include <unordered_map>

namespace rpg {

class World {
public:
    World();

    void add(Location location);
    const Location* find(const std::string& id) const;
    std::size_t size() const;

private:
    std::unordered_map<std::string, Location> locations_;
};

World make_demo_world();

}  // namespace rpg

#endif
```

Ключевое поле — `locations_` типа `unordered_map<string, Location>`. Карта строка-в-локацию. `World` владеет всеми локациями — они хранятся **по значению** внутри карты. При уничтожении `World` все локации уничтожатся (RAII!).

### find возвращает указатель

```cpp
const Location* World::find(const std::string& id) const;
```

Возвращает `const Location*`. Почему указатель, а не ссылка?

Потому что **может не найтись**. Ссылка не может быть «никакой» — она всегда на что-то указывает. Указатель может быть `nullptr`. В нашем случае:

```cpp
const Location* loc = world_.find("unknown_id");
if (loc == nullptr) {
    // не найдено
}
```

Это вариант **«optional»**. В C++17 есть `std::optional<T>`, но в C++11 — обходимся указателями.

### Заводская функция

```cpp
World make_demo_world();
```

Это **свободная функция** в `namespace rpg`, не метод класса. Создаёт демо-мир из трёх локаций. Возвращает `World` по значению.

В реализации:

```cpp
World make_demo_world() {
    World w;

    Location glade("glade", "Лесная поляна", "...");
    glade.add_exit("west", "village");
    glade.add_exit("east", "forest");

    Location village("village", "Деревня", "...");
    village.add_exit("east", "glade");

    Location forest("forest", "Тёмный лес", "...");
    forest.add_exit("west", "glade");

    w.add(std::move(glade));
    w.add(std::move(village));
    w.add(std::move(forest));

    return w;
}
```

Создаём `World`, три `Location`, перемещаем их в `World`, возвращаем сам `World`. `std::move` тут потому, что после `add` локальные `glade`, `village`, `forest` не нужны — пусть `World` забирает их «внутренности» без копирования.

Возврат `World` по значению — тоже без копирования благодаря **RVO** (return value optimization), о которой говорили в главе 10. Компилятор просто строит `World` сразу в нужном месте.

### emplace вместо insert+move

В `World::add`:

```cpp
void World::add(Location location) {
    std::string id = location.id();
    locations_.emplace(std::move(id), std::move(location));
}
```

`emplace` — это «вставка с конструированием на месте». Эффективнее `insert`:

- `insert(std::make_pair(id, location))` сначала создаёт пару, потом копирует/перемещает её в карту.
- `emplace(id, location)` пересылает аргументы прямо в конструктор пары внутри карты. Никаких промежуточных копий.

Тонкость: `emplace` ожидает аргументы конструктора `std::pair<key, value>`, и таких пар бывает два вида (с разными порядками копирования/перемещения). На наших размерах локаций разница незаметна, но привычка `emplace` вместо `insert` полезна.

### Default-конструктор через = default

```cpp
World::World() = default;
```

Я мог бы написать `World::World() {}` — пустое тело. Но `= default` явно говорит «дефолтная реализация». Тонкие отличия (для агрегатной инициализации и других вещей) для нас сейчас неважны; стиль с `= default` — более явный.

## Game: всё вместе

Обновлённый `Game` теперь владеет `Player` и `World`. Карта команд расширилась.

### Объявление

```cpp
class Game {
public:
    Game();
    int run();

private:
    bool step();
    void execute(const std::string& cmd);

    void cmd_help() const;
    void cmd_look() const;
    void cmd_go(const std::string& direction);
    void cmd_status() const;

    bool running_;
    World world_;
    Player player_;
    std::string current_location_id_;
};
```

Каждая команда — отдельный метод. `cmd_help`, `cmd_look`, `cmd_status` — не меняют состояние (`const`). `cmd_go` меняет (`current_location_id_`).

### Конструктор

```cpp
Game::Game()
    : running_(true),
      world_(make_demo_world()),
      player_("Герой", 20),
      current_location_id_("glade") {
}
```

Все поля инициализируются в списке. `world_(make_demo_world())` — вызов заводской функции, она возвращает `World` по значению, `world_` инициализируется этим значением (через move-конструктор `World`, который компилятор сгенерировал нам автоматически — rule of zero!).

### Команды

```cpp
void Game::execute(const std::string& cmd) {
    std::istringstream ss(cmd);
    std::string verb;
    ss >> verb;

    if (verb == "help") {
        cmd_help();
    } else if (verb == "look" || verb == "l") {
        cmd_look();
    } else if (verb == "go") {
        std::string dir;
        ss >> dir;
        if (dir.empty()) {
            std::cout << "Куда идти?\n";
        } else {
            cmd_go(dir);
        }
    } else if (verb == "status" || verb == "stat") {
        cmd_status();
    } else if (verb == "quit" || verb == "q" || verb == "exit") {
        running_ = false;
    } else {
        std::cout << "Неизвестная команда\n";
    }
}
```

Распознавание команд — пока через `if/else`. В главе 21 заменим на словарь имя → лямбда. Сейчас простой код.

```cpp
void Game::cmd_look() const {
    const Location* loc = world_.find(current_location_id_);
    if (!loc) {
        std::cout << "Где-то в пустоте.\n";
        return;
    }
    std::cout << "== " << loc->name() << " ==\n";
    std::cout << loc->description() << "\n";

    if (!loc->exits().empty()) {
        std::cout << "Выходы:";
        for (const auto& exit_pair : loc->exits()) {
            std::cout << " " << exit_pair.first;
        }
        std::cout << "\n";
    }
}
```

`world_.find(current_location_id_)` — получаем указатель. Проверяем на `nullptr`. Если есть — печатаем имя, описание, выходы.

`for (const auto& exit_pair : loc->exits())` — стандартная идиома обхода карты по const-ссылке.

```cpp
void Game::cmd_go(const std::string& direction) {
    const Location* loc = world_.find(current_location_id_);
    if (!loc) return;

    std::string target = loc->exit(direction);
    if (target.empty()) {
        std::cout << "Туда не пройти.\n";
        return;
    }

    current_location_id_ = target;
    cmd_look();
}
```

Получаем текущую локацию, ищем выход в нужном направлении, если есть — меняем `current_location_id_`. После — `cmd_look()` показывает новую локацию.

## Сборка и запуск

```bash
$ make clean && make
g++ ... -c src/main.cpp -o build/main.o
g++ ... -c src/game.cpp -o build/game.o
g++ ... -c src/location.cpp -o build/location.o
g++ ... -c src/player.cpp -o build/player.o
g++ ... -c src/world.cpp -o build/world.o
g++ ... build/main.o build/game.o build/location.o build/player.o build/world.o -o build/rpg
Built: build/rpg (debug)
```

Заметили? `make` сам подхватил новые `.cpp` файлы — `wildcard src/*.cpp` в Makefile собрал их все. Никаких ручных правок.

```bash
$ ./build/rpg
=== RPG-демо ===
Привет, Герой! Введите 'help' для справки.

== Лесная поляна ==
Залитая солнцем поляна. На западе видна тропа к деревне, на востоке — тёмный лес.
Выходы: east west

> status
Герой: HP 20/20

> go east
== Тёмный лес ==
Ветви скрипят над головой. Здесь сумрачно и тихо. На запад уходит тропа.
Выходы: west

> go west
== Лесная поляна ==
Залитая солнцем поляна. На западе видна тропа к деревне, на востоке — тёмный лес.
Выходы: east west

> go north
Туда не пройти.

> quit
До свидания.
```

Игра. Не богатая, но игра.

## Что освоено в этой главе

1. **`class` vs `struct`** — разница в умолчании доступа.
2. **Поля, методы, инкапсуляция** — `private`/`public`.
3. **Конвенция `_` в конце поля** — отличает от параметров.
4. **Конструктор и инициализационный список** — правильный способ инициализации полей.
5. **`std::move` в конструкторе** — для дорогих параметров.
6. **`this`** — указатель на текущий объект.
7. **`const`-методы** — обещание «не мутирую состояние».
8. **`= default` и `= delete`** — явные специальные функции.
9. **Rule of zero / rule of three / rule of five** — иерархия правил.
10. **Возврат указателя из `find`** — паттерн для «может не найтись».
11. **`emplace` вместо `insert`** — без промежуточных копий.

## Маленькое упражнение

1. Добавьте в `Player` поле `int xp_` (опыт). Метод `void gain_xp(int amount)` и `int xp() const`. В `cmd_status` показывайте опыт.

2. Добавьте в игру новую локацию — пещеру (`cave`), доступную с лесной поляны на юг. Из пещеры можно вернуться на север.

3. Добавьте команду `where` — выводит ID текущей локации (для дебага).

4. Сделайте `Player::heal` так, чтобы он отказывался лечить мёртвого игрока (если `hp_ == 0`, ничего не делать, выдать сообщение).

5. Перепишите `Location::exit` так, чтобы он бросал `std::out_of_range`, если направление не найдено. Сравните с текущей версией — какая удобнее для нашего `cmd_go`?

6. (Сложнее) Добавьте `Location::add_item(std::string item_name)` и `std::vector<std::string> items()`. В `cmd_look` показывайте список предметов: «На полу лежит: меч, золото».

7. (Сложнее) Прочитайте код `World::add`. Объясните, почему `std::string id = location.id();` копирует строку, а `std::move(id)` дальше уже её перемещает.

## Что дальше

Глава 17 — **наследование и виртуальные функции**. Создадим иерархию `Entity → Player/Enemy/NPC` и параллельную `Item → Weapon/Armor/Consumable`. Это полиморфизм — главная идея ООП. Узнаем, что такое vtable, почему виртуальный деструктор обязателен, что такое `override` и `final`.

После 17-й главы у нас будут враги, оружие, броня. Бои добавятся в главе 18 (заодно введём `unique_ptr` для управления предметами).

Темп Части II: новый класс или две — каждую главу, постепенно. К 25-й главе соберём всё в живую игру.
