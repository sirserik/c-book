# Глава 20. Шаблоны — EventBus

В этой главе — **шаблоны** (templates), одна из самых мощных и одновременно самых страшных частей C++. Шаблоны позволяют писать код, работающий с **любым типом**, без потери производительности. STL без них не было бы — `std::vector<int>` и `std::vector<std::string>` это два разных типа, но кода в стандартной библиотеке для них один.

Мы построим `EventBus<T>` — шаблонную шину событий. Игровые подсистемы смогут публиковать события (`ItemPickedEvent`, `PlayerMovedEvent`), а подписчики получать колбэки. Это паттерн **observer**, реализованный обобщённо.

Главное обещание главы: после неё вы будете понимать большую часть кода стандартной библиотеки, плюс уметь писать свои простые шаблоны. Это шаг от «пользователя C++» к «продвинутому пользователю».

## Зачем шаблоны

Вернёмся к функции max:

```cpp
int max(int a, int b) {
    return a > b ? a : b;
}
```

Работает для `int`. А для `double`? Нужна другая функция. Для `std::string`? Третья. Для какого-нибудь `Player`-а с оператором `<`? Четвёртая.

В Python с этим проще: `def max(a, b): return a if a > b else b`. Один код для всего.

В C++ можно перегрузкой:

```cpp
int max(int a, int b) { return a > b ? a : b; }
double max(double a, double b) { return a > b ? a : b; }
std::string max(const std::string& a, const std::string& b) { return a > b ? a : b; }
```

Три функции, идентичный код. Если потребуется четвёртый тип — четвёртая функция. Это копи-паст.

**Шаблон** — это «функция-фабрика». Вы пишете один раз, компилятор сам создаёт версии для нужных типов:

```cpp
template <typename T>
T max(T a, T b) {
    return a > b ? a : b;
}
```

`template <typename T>` объявляет шаблон. `T` — параметр-тип. Может быть `int`, `double`, чем угодно.

Использование:

```cpp
int a = max(3, 7);                   // T выводится как int
double b = max(3.14, 2.71);          // T выводится как double
std::string c = max(std::string("apple"), std::string("banana"));  // T = std::string

// Можно явно указать:
int d = max<int>(3, 7);
```

Компилятор для каждого использования создаёт реальную функцию: `max<int>`, `max<double>`, и так далее. Это называется **инстанциация** шаблона.

После компиляции в бинарнике лежат именно сгенерированные конкретные функции. Сам шаблон — это «рецепт», не существующий в финальном коде.

## typename vs class

В `template <typename T>` слово `typename` означает «следующий идентификатор — это тип-параметр». Можно также писать `class`:

```cpp
template <typename T>
T max(T a, T b);

template <class T>
T max(T a, T b);
```

— оба варианта **полностью эквивалентны**. Различий нет.

Исторически: ключевое слово `class` для шаблонов появилось первым, и до C++ не было отдельного слова для «параметра-типа». Потом ввели `typename` как более понятное. Сейчас обычно пишут **`typename`** — оно лучше отражает смысл. Но в чужом коде увидите оба.

В шаблоне `T` может быть не только пользовательским типом, но и встроенным (`int`, `double`), даже указателем (`int*`). Главное — у него должны быть операции, которые шаблон использует. У нас `max` использует `operator>` — значит, у `T` должна быть эта операция. Иначе компилятор выдаст ошибку при инстанциации.

## Где живут шаблоны: только в заголовках

В главе 7 я говорил: объявления в `.h`, определения в `.cpp`. Для шаблонов это **не работает**.

Шаблоны должны быть **полностью видны** в момент инстанциации. То есть тело шаблона должно быть в заголовочном файле. Если положить `template T max(T a, T b) { return ... }` в `.cpp` — другой файл, использующий `max<int>`, не увидит реализацию и выдаст ошибку линковки.

Поэтому шаблоны живут **полностью в `.h`-файле**:

```cpp
// math_utils.h
#ifndef MATH_UTILS_H
#define MATH_UTILS_H

template <typename T>
T max(T a, T b) {
    return a > b ? a : b;
}

#endif
```

Никакого `math_utils.cpp`. Реализация в заголовке.

Это имеет последствия:

- **Раздувание кода**. Каждый `.cpp`, использующий шаблон, перекомпилирует его тело. Это медленно.
- **Утечка реализации**. Все детали шаблона видны пользователю.
- **Реальная инстанциация в нескольких translation units**. Линкер потом убирает дубликаты (через `inline`, шаблоны автоматически).

Альтернатива — **explicit instantiation**: явно говорим, какие версии шаблона нужны, генерируем их в одном `.cpp`. Сложновато; в этой книге не пользуемся.

## Шаблоны классов

То же с классами:

```cpp
template <typename T>
class Vec {
public:
    Vec() : data_(nullptr), size_(0), cap_(0) {}
    
    void push_back(const T& value) {
        if (size_ == cap_) grow();
        data_[size_++] = value;
    }
    
    T& operator[](std::size_t i) { return data_[i]; }
    
    std::size_t size() const { return size_; }
    
private:
    void grow() { /* ... */ }
    
    T* data_;
    std::size_t size_;
    std::size_t cap_;
};
```

Это сокращённый `std::vector`. Использование:

```cpp
Vec<int> v;
v.push_back(1);
v.push_back(2);
std::cout << v[0] << "\n";   // 1
```

`Vec<int>` — это **тип**, сгенерированный из шаблона с `T = int`. У него есть свои методы, конструкторы, всё как у обычного класса.

Когда вы пишете методы шаблонного класса **вне** класса, синтаксис громоздкий:

```cpp
template <typename T>
class Vec {
public:
    void push_back(const T& value);
};

// Реализация:
template <typename T>
void Vec<T>::push_back(const T& value) {
    // ...
}
```

Каждое определение метода требует префикса `template <typename T>` и `Vec<T>::`. Если параметров шаблона много — становится трудно читать. Многие пишут все методы прямо внутри класса (как у нашего `EventBus`), чтобы не повторять.

## Дедукция типа

Когда вы вызываете `max(3, 7)`, компилятор сам выводит `T = int`. Это **template argument deduction**.

Деюция работает по аргументам функции. Если параметр объявлен `T`, и вы передаёте `int` — `T = int`. Если параметр `const T&`, передаёте `int` — `T = int` всё равно (`const &` отбрасывается при дедукции).

Иногда дедукция не работает — например, для возвращаемого типа:

```cpp
template <typename T>
T from_string(const std::string& s);

int i = from_string<int>("42");      // явно: T = int
int j = from_string("42");            // ОШИБКА: не из чего вывести T
```

Тогда указываем явно: `from_string<int>("42")`.

Для классов в C++11 дедукция не работала (`std::vector v{1, 2, 3}` — нужен был `std::vector<int> v{1, 2, 3}`). С C++17 добавили **CTAD** (class template argument deduction), но это не наша база.

## Специализация

Иногда хочется для **одного конкретного** типа сделать особую реализацию. Это **специализация шаблона**.

Пример. У нас шаблон `print<T>`:

```cpp
template <typename T>
void print(const T& value) {
    std::cout << value;
}
```

Для `bool` хотим выводить `"true"`/`"false"`, а не `1`/`0`:

```cpp
template <>   // явная специализация
void print<bool>(const bool& value) {
    std::cout << (value ? "true" : "false");
}
```

`template <>` (пустые скобки) — синтаксис **полной специализации**. Дальше `print<bool>` указывает, какой тип специализируем. Когда компилятор увидит `print(true)` — выберет специализацию, не общий шаблон.

Использование:

```cpp
print(42);     // "42"
print(true);   // "true"
print("hello"); // "hello"
```

В стандартной библиотеке полно специализаций. Например, `std::vector<bool>` — особая реализация (упаковывает в биты), отличающаяся от `std::vector<T>` для других типов.

### Частичная специализация

Только для классов (для функций нет — пишут перегрузки). Допустим, у нас шаблон `Wrapper<T, N>`, и для случая `N == 0` нужна особая реализация:

```cpp
template <typename T, int N>
class Wrapper {
public:
    void show() { std::cout << "general " << N << "\n"; }
};

// Частичная специализация для N = 0
template <typename T>
class Wrapper<T, 0> {
public:
    void show() { std::cout << "specialized: zero\n"; }
};

Wrapper<int, 5> a;  a.show();  // "general 5"
Wrapper<int, 0> b;  b.show();  // "specialized: zero"
```

В нашей книге частичная специализация почти не появится — это глубокая тема. Просто знайте, что она есть.

## Variadic templates

Шаблон может принимать **переменное число** параметров:

```cpp
template <typename... Args>
void print_all(Args... args) {
    // ...
}
```

`Args...` — это «пакет параметров» (parameter pack). Принимает любое количество типов. Можно вызвать:

```cpp
print_all(1, 2.5, "hello", true);   // 4 параметра
print_all();                          // 0 параметров
```

Внутри функции пакет нужно «развернуть». Самый частый способ — рекурсия:

```cpp
void print_all() {
    std::cout << "\n";
}

template <typename T, typename... Rest>
void print_all(T first, Rest... rest) {
    std::cout << first << " ";
    print_all(rest...);   // рекурсивный вызов
}

int main() {
    print_all(1, 2.5, "hello", true);
}
```

Каждый вызов «отрывает» первый элемент и идёт дальше. В конце — пустая база (terminator).

Вывод: `1 2.5 hello 1 ` (true печатается как 1).

В C++17 появилось **fold expression** — более короткий синтаксис, но он не наша база.

### Variadic в нашем make_unique

Помните `rpg::make_unique`?

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

`Args... args` — пакет аргументов любых типов. `std::forward<Args>(args)...` — развёртывание пакета. Каждый `args` пересылается через `std::forward`, который сохраняет lvalue/rvalue-ность.

Это **perfect forwarding** в действии. Можем создавать любые объекты с любыми параметрами конструктора:

```cpp
auto p1 = make_unique<int>(42);
auto p2 = make_unique<std::string>(10, 'x');   // строка из 10 'x'
auto p3 = make_unique<Player>("Hero", 100);
```

И всё это **без копирования** параметров — они пересылаются как есть.

## SFINAE — кратко

«Substitution Failure Is Not An Error». Один из главных механизмов шаблонной магии.

Идея: если при подстановке типа в шаблон возникает ошибка — компилятор не выдаёт error, а **молча игнорирует** этот шаблон. Это позволяет писать шаблоны, работающие только с определёнными типами.

Пример:

```cpp
template <typename T>
typename std::enable_if<std::is_integral<T>::value, T>::type
square(T x) {
    return x * x;
}

square(5);       // OK
square(3.14);    // ОШИБКА: square отброшен, потому что double не integral
```

`std::enable_if` — шаблон-машина: если первый параметр истина, есть тип `::type`; иначе ошибка. SFINAE ловит «нет ::type», молча отбрасывает шаблон.

Синтаксис страшный. В C++20 для этого появились **концепты** (concepts), которые гораздо чище. В C++11 — только SFINAE.

В нашей книге SFINAE не используем. Это упоминание для тех, кто будет читать чужой код.

## Ошибки шаблонов

Если в шаблонном коде ошибка — компилятор выдаёт **очень длинные** сообщения. Иногда десятки строк на одну ошибку.

```cpp
template <typename T>
T sum(const T& a, const T& b) {
    return a + b;
}

struct Foo {};

int main() {
    Foo a, b;
    sum(a, b);
}
```

Компилятор:

```
error: no match for 'operator+' (operand types are 'const Foo' and 'const Foo')
note: candidate: 'T sum(const T&, const T&) [with T = Foo]'
note: in instantiation of function template specialization 'sum<Foo>' requested here
...
```

Видны и шаблон, и место использования. На сложных шаблонах (`std::vector<std::map<int, std::vector<...>>>`) ошибка может занять страницу.

**Совет**: читайте сообщение **снизу вверх**. Снизу — место ошибки. Над ним — что попытался сделать компилятор. И там, где конкретно «не подошло» — оригинальная причина.

В C++20 концепты делают сообщения чище. Но C++11 — терпите.

## EventBus<T>

Соберём всё в нашем `EventBus`. Это шаблонный класс, который для каждого типа события держит свой список колбэков:

```cpp
template <typename Event>
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(Handler handler) {
        handlers_.push_back(std::move(handler));
    }

    void publish(const Event& event) const {
        for (const auto& h : handlers_) {
            h(event);
        }
    }

    std::size_t subscriber_count() const {
        return handlers_.size();
    }

private:
    std::vector<Handler> handlers_;
};
```

Разберём.

**`template <typename Event>`** — шаблон по типу события.

**`using Handler = std::function<void(const Event&)>;`** — псевдоним типа. `Handler` — это «функция, принимающая `const Event&` и не возвращающая ничего». `std::function` — тип-«стиратель» (type-erased wrapper), который умеет хранить любую callable: лямбду, функтор, указатель на функцию. Подробно — в главе 21.

`using` — современная альтернатива `typedef`:

```cpp
typedef std::function<void(const Event&)> Handler;   // старо
using Handler = std::function<void(const Event&)>;    // ново, читается лучше
```

В шаблонных классах `using` особенно ценен, потому что работает с шаблонными параметрами без проблем.

**`subscribe`** добавляет обработчик в вектор. Перемещаем, чтобы избежать копии.

**`publish`** проходит по всем и вызывает. Метод `const` — публикация не меняет список подписчиков.

Простой, но работающий код. Около 25 строк.

### События

Сами события — просто структуры:

```cpp
// events.h
struct ItemPickedEvent {
    std::string player_name;
    std::string item_name;
};

struct PlayerMovedEvent {
    std::string player_name;
    std::string from_id;
    std::string to_id;
};
```

Никакой иерархии, ничего сложного. Каждый тип события — своя структура. EventBus параметризируется конкретным типом, и события только этого типа в неё попадают.

### Тест

```cpp
EventBus<ItemPickedEvent> pickup_bus;
EventBus<PlayerMovedEvent> move_bus;

// Подписчик-логгер
pickup_bus.subscribe([](const ItemPickedEvent& e) {
    std::cout << "[log] " << e.player_name << " поднял " << e.item_name << "\n";
});

// Второй подписчик — ачивка за первый предмет
bool first = true;
pickup_bus.subscribe([&first](const ItemPickedEvent& e) {
    if (first) {
        std::cout << "[ачивка] первый предмет!\n";
        first = false;
    }
});

move_bus.subscribe([](const PlayerMovedEvent& e) {
    std::cout << "[log] " << e.player_name << ": " 
              << e.from_id << " -> " << e.to_id << "\n";
});

pickup_bus.publish({"Герой", "ржавый меч"});
pickup_bus.publish({"Герой", "зелье"});
move_bus.publish({"Герой", "glade", "village"});
```

Каждая лямбда — это объект `std::function<void(const Event&)>`. Лямбды и `std::function` — глава 21.

Вывод:

```
[log] Герой поднял ржавый меч
[ачивка] первый предмет!
[log] Герой поднял зелье
[log] Герой: glade -> village
```

Каждое `publish` вызвало всех подписчиков по очереди.

## Концепции и применения

Что нам это даёт?

1. **Развязывание подсистем**. Бой не знает про логирование, но при ударе шлёт `DamageEvent`. Логгер подписан на `DamageEvent` и сам решает, что писать.
2. **Лёгкость расширения**. Новая фича (например, «достижения за 100 убийств») — отдельный подписчик. Старый код не меняется.
3. **Тестирование**. Подписать тест-подписчика — проверить, что событие выстрелило.

Это паттерн **observer** или **pub-sub**. На больших играх — основа архитектуры. В нашей книге используется частично.

## Интеграция в проект (предварительно)

Я не делаю интеграцию `EventBus` в `Game` в этой главе — добавим в финальной (25). Но архитектура такая:

```cpp
class Game {
private:
    EventBus<ItemPickedEvent> pickup_bus_;
    EventBus<PlayerMovedEvent> move_bus_;
    // ...
    
    void cmd_take(const std::string& name) {
        // ... поднимаем предмет ...
        pickup_bus_.publish({player_.name(), item_name});
    }
    
    void cmd_go(const std::string& dir) {
        // ... меняем локацию ...
        move_bus_.publish({player_.name(), old_id, new_id});
    }
};
```

В начале программы — подписываемся:

```cpp
Game game;
game.subscribe_pickup([](const auto& e) { ... });
```

Удобно? Очень. Но в нашей маленькой игре он не настолько критичен — оставим этот рефакторинг на потом.

## Сторонние шаблонные паттерны

Несколько других мест, где шаблоны блистают.

### Тип-безопасное хранение разнотипных данных

```cpp
template <typename T>
class Holder {
public:
    Holder(T value) : value_(std::move(value)) {}
    T& get() { return value_; }
    const T& get() const { return value_; }
private:
    T value_;
};

Holder<int> a(42);
Holder<std::string> b("hi");

a.get() += 1;        // a содержит 43
b.get() += " world"; // b содержит "hi world"
```

### Типовые функции

```cpp
template <typename Container, typename Predicate>
auto count_if(const Container& c, Predicate p) -> std::size_t {
    std::size_t n = 0;
    for (const auto& x : c) {
        if (p(x)) ++n;
    }
    return n;
}

std::vector<int> v = {1, 2, 3, 4, 5};
auto evens = count_if(v, [](int x) { return x % 2 == 0; });
```

(Стандарт уже даёт `std::count_if`, но мы могли бы написать свой.)

### Generic algorithms

Вся STL — шаблоны. `std::sort`, `std::find`, `std::accumulate` — все принимают любой контейнер и любые типы. Это даёт огромную переиспользуемость.

## Шаблоны и компилятор

Под капотом шаблоны работают так:

1. **На этапе парсинга** компилятор проверяет синтаксис шаблона, но **не компилирует его в код**. Просто записывает «вот рецепт».

2. **При использовании** (`max<int>(3, 7)`) компилятор делает **инстанциацию** — берёт рецепт, подставляет `T = int`, и компилирует получившуюся конкретную функцию.

3. **Каждая инстанциация — новая функция**. `max<int>` и `max<double>` — два разных символа в объектном файле.

4. **Линкер убирает дубликаты**. Если два `.cpp`-файла оба используют `max<int>` — оба генерируют его, но линкер оставляет одну копию.

Поэтому в больших проектах с шаблонами:
- **Долгая компиляция** — каждый файл компилирует все используемые шаблоны.
- **Большой код** — много дубликатов до линковки.
- **Сложные сообщения об ошибках** — раскрыт каждый уровень шаблона.

Современные компиляторы это знают и оптимизируют. Но факт остаётся: шаблоны не бесплатны на compile-time. На run-time же они часто **быстрее** обычных функций — компилятор знает все типы и может агрессивно инлайнить.

## Главные правила главы

1. **Шаблоны для обобщённого кода.** Если есть N копий функции, отличающейся только типом — шаблон.
2. **Тело шаблона в `.h`-файле.** Реализации в `.cpp` не работают для шаблонов.
3. **`typename` или `class`** — выбирайте `typename`, оно лучше отражает смысл.
4. **Дедукция типа** работает для функций. Можно явно: `func<int>(arg)`.
5. **`using` лучше `typedef`** для псевдонимов типов.
6. **`...` для variadic** — переменное число параметров. Используется через рекурсию.
7. **Ошибки шаблонов** читайте снизу вверх.
8. **Шаблоны не бесплатны на compile-time** — длительная сборка на больших проектах.
9. **`std::function` для type-erased callable** — подробнее в следующей главе.

## Маленькое упражнение

1. Напишите шаблон `min<T>(T a, T b)` по аналогии с `max`. Используйте для `int`, `double`, `std::string`.

2. Напишите шаблон `swap<T>(T& a, T& b)`. Сравните со `std::swap`.

3. Напишите шаблон `print_pair<A, B>(const std::pair<A, B>& p)`, выводящий `"(first, second)"`.

4. Сделайте `EventBus` методом `clear()` — убрать всех подписчиков.

5. Добавьте к `EventBus` метод `subscribe_once(handler)` — обработчик удалится после первого вызова. Подсказка: можно через флаг внутри лямбды, или специальной обёртке.

6. (Сложнее) Сделайте шаблон `Stack<T>` — стек на массиве с операциями `push`, `pop`, `top`, `empty`, `size`. Используйте `std::vector<T>` внутри.

7. (Сложнее) Создайте шаблон `Function<R(Args...)>` — упрощённый аналог `std::function`. Поддерживает только хранение лямбд без захвата. Это сложно — подсказка: внутри используйте `R (*func_)(Args...)` (указатель на функцию).

## Что дальше

Глава 21 — **лямбды и `std::function`**. Мы уже много раз использовали лямбды, теперь разберём детально: захваты (`[]`, `[=]`, `[&]`, `[this]`, `[var]`, `[&var]`), `mutable`, тип лямбды, `std::function` как тип-стиратель, performance trade-offs. После — исключения (22), парсер мира (23), сохранения (24), финал RPG (25).
