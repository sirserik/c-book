# Глава 14. Алгоритмы и итераторы

В прошлой главе мы освоили контейнеры — где хранить данные. Эта глава — про то, что с ними **делать**: искать, сортировать, преобразовывать, сворачивать. Всё это уже написано в стандартной библиотеке через `<algorithm>` и `<numeric>`. Самим алгоритмы писать почти никогда не нужно — `std::sort` от Google написан умнее, чем любая реализация на коленке за вечер. Ваша работа — научиться этими готовыми штуками пользоваться.

После этой главы Часть I закрывается, и мы стартуем RPG-проект.

## Зачем алгоритмы — снова про переиспользование

Допустим, у вас есть `std::vector<int>` и нужно найти, есть ли там значение 42. Можно написать вручную:

```cpp
bool found = false;
for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == 42) {
        found = true;
        break;
    }
}
```

Или короче:

```cpp
auto it = std::find(v.begin(), v.end(), 42);
bool found = (it != v.end());
```

Второе короче, читается яснее, и не зависит от того, `vector` у вас или `list` или `array`. `std::find` работает с **любым** контейнером, у которого есть итераторы. Это и есть главная идея STL — **алгоритмы не привязаны к контейнерам**.

## Итераторы — обобщённый указатель

В прошлой главе я обещал детали по итераторам. Итак.

**Итератор** — это объект, который ведёт себя как указатель на элемент контейнера:
- `*it` — получить значение.
- `++it` — перейти к следующему.
- `it1 == it2` / `it1 != it2` — сравнить.

В разных контейнерах итераторы устроены по-разному:
- В `vector` — это просто `T*` (или почти).
- В `list` — указатель на узел.
- В `map` — указатель на узел дерева.

Но интерфейс одинаков. Алгоритм типа `std::find` пишется в общем виде через шаблоны:

```cpp
template <typename Iterator, typename Value>
Iterator find(Iterator begin, Iterator end, const Value& value) {
    for (; begin != end; ++begin) {
        if (*begin == value) return begin;
    }
    return end;
}
```

(Шаблоны мы подробно разберём в главе 20, но идею вы уже видите.)

Алгоритм не знает, что за контейнер. Знает только, что итератор умеет `++`, `*` и `==`. Поэтому один и тот же `std::find` работает с `vector`, `list`, `map`, и так далее.

### Что такое `begin()` и `end()`

`v.begin()` — итератор на **первый** элемент.

`v.end()` — итератор **за последний** (sentinel, «маркер конца»). Это **не последний элемент**, а позиция «после последнего». Разыменовывать `end()` — UB.

Размер контейнера: `end() - begin()` (для случайно-доступных итераторов).

Пустой контейнер: `begin() == end()`.

«Полуоткрытый интервал» `[begin, end)` — стандарт для всех алгоритмов STL. Принимайте как данность.

## Категории итераторов

Не все итераторы одинаково мощные. STL делит их на **категории**, и каждый алгоритм требует определённой категории.

### Input iterator

Самый слабый. Можно только **читать**, и только **один раз**:
- `*it` — прочитать.
- `++it` — перейти к следующему.
- Сравнивать.

Пример: итератор `std::istream_iterator` (читает из потока).

Со слабым итератором работают алгоритмы типа `find`, `count` — они один раз проходят и не возвращаются.

### Output iterator

Можно только **писать**:
- `*it = value` — записать.
- `++it` — перейти.

Пример: `std::ostream_iterator` (пишет в поток).

С слабым output итератором работают `copy`, `transform` (пишущие куда-то).

### Forward iterator

Можно читать **много раз**, перебирать в одну сторону.

Пример: итераторы `std::forward_list`.

### Bidirectional iterator

Прямой плюс возможность идти **назад** (`--it`).

Пример: итераторы `std::list`, `std::map`, `std::set`.

### Random access iterator

Самый мощный. Можно прыгать на N позиций (`it + N`, `it - N`), вычислять разность, индексировать.

Пример: итераторы `std::vector`, `std::deque`, `std::array`, сырые указатели.

Со случайно-доступными работает `std::sort`, потому что ему нужно прыгать туда-сюда.

### Иерархия

```
Random access  →  Bidirectional  →  Forward  →  Input/Output
   (vector)         (list)          (flist)     (потоки)
```

Каждая следующая категория — расширение предыдущей. Random access умеет всё, что Forward, плюс прыжки.

Если алгоритм требует Random access, а у вас Forward — не сработает. Поэтому `std::sort` нельзя применить к `std::list`. У `list` есть свой метод `l.sort()` для этого случая.

В большинстве кода вы не задумываетесь о категориях. Знаете только: «`vector` и `array` — Random access, `list` и `map` — Bidirectional, остальное реже».

## Алгоритмы без модификации

Не меняют контейнер, только читают.

### find — найти первый

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};

auto it = std::find(v.begin(), v.end(), 3);
if (it != v.end()) {
    int index = it - v.begin();   // для random access
    std::cout << "Found at index " << index << "\n";
}
```

### find_if — найти с предикатом

«Предикат» — это функция или лямбда, возвращающая `bool`. Найти первый элемент, удовлетворяющий условию:

```cpp
auto it = std::find_if(v.begin(), v.end(),
                       [](int x) { return x > 3; });
if (it != v.end()) {
    std::cout << "Первый > 3: " << *it << "\n";   // 4
}
```

`[](int x) { return x > 3; }` — это **лямбда**, безымянная функция. Глава 21 — детально. Сейчас можно читать как «функция, принимающая `int` и возвращающая, больше ли он трёх».

### count и count_if

```cpp
std::vector<int> v = {1, 2, 2, 3, 2, 4};

int n = std::count(v.begin(), v.end(), 2);   // 3 — сколько двоек
int m = std::count_if(v.begin(), v.end(),
                      [](int x) { return x > 2; });   // 2 — сколько > 2
```

### all_of, any_of, none_of

Проверки «все», «хоть один», «ни один»:

```cpp
bool all_positive  = std::all_of(v.begin(), v.end(),
                                  [](int x) { return x > 0; });
bool has_negative  = std::any_of(v.begin(), v.end(),
                                  [](int x) { return x < 0; });
bool no_zeros      = std::none_of(v.begin(), v.end(),
                                   [](int x) { return x == 0; });
```

### min_element и max_element

Найти минимальный и максимальный:

```cpp
std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};

auto min_it = std::min_element(v.begin(), v.end());
auto max_it = std::max_element(v.begin(), v.end());

std::cout << "Min: " << *min_it << "\n";   // 1
std::cout << "Max: " << *max_it << "\n";   // 9
```

`std::min`/`std::max` — это просто между двумя значениями:

```cpp
int x = std::min(5, 7);   // 5
int y = std::max({5, 3, 9, 2});   // 9 — из списка (C++11)
```

### equal — сравнить два диапазона

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {1, 2, 3};

bool same = std::equal(a.begin(), a.end(), b.begin());   // true
```

## Алгоритмы с модификацией

Меняют содержимое контейнера.

### fill — заполнить значением

```cpp
std::vector<int> v(10);
std::fill(v.begin(), v.end(), 42);
// v = {42, 42, 42, 42, 42, 42, 42, 42, 42, 42}
```

### iota — заполнить последовательно

```cpp
#include <numeric>

std::vector<int> v(5);
std::iota(v.begin(), v.end(), 10);
// v = {10, 11, 12, 13, 14}
```

«iota» — старое слово, означает «единица» (греческая буква йота). Алгоритм заполняет начиная с указанного значения с шагом 1.

### transform — применить функцию

Применить функцию к каждому элементу:

```cpp
std::vector<int> v = {1, 2, 3, 4};
std::vector<int> doubled(v.size());

std::transform(v.begin(), v.end(),
               doubled.begin(),
               [](int x) { return x * 2; });
// doubled = {2, 4, 6, 8}
```

Параметры: начало входа, конец входа, начало выхода, функция.

Можно писать в **тот же** контейнер:

```cpp
std::transform(v.begin(), v.end(),
               v.begin(),
               [](int x) { return x * 2; });
// v = {2, 4, 6, 8}
```

### copy и copy_if

Копировать элементы:

```cpp
std::vector<int> src = {1, 2, 3, 4, 5};
std::vector<int> dst(5);

std::copy(src.begin(), src.end(), dst.begin());
// dst = {1, 2, 3, 4, 5}
```

С предикатом — только подходящие:

```cpp
std::vector<int> src = {1, 2, 3, 4, 5, 6};
std::vector<int> evens;

std::copy_if(src.begin(), src.end(),
             std::back_inserter(evens),
             [](int x) { return x % 2 == 0; });
// evens = {2, 4, 6}
```

`std::back_inserter(evens)` — особый итератор, который при записи вызывает `evens.push_back()`. Если бы мы написали `evens.begin()`, было бы UB — мы бы писали в пустой вектор.

### remove и remove_if

Хитрая семантика: эти алгоритмы **не удаляют** элементы (контейнер они не знают). Они **сдвигают** «оставляемые» в начало и возвращают итератор «нового конца». Хвост остаётся в неопределённом состоянии.

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
auto new_end = std::remove(v.begin(), v.end(), 3);
// v = {1, 2, 4, 5, ?}   (5 элементов всё ещё, последний — мусор)
// new_end указывает на позицию «после 5»

v.erase(new_end, v.end());
// v = {1, 2, 4, 5}
```

Эта пара `remove` + `erase` — идиома **erase-remove**, мы её видели в главе 13. Запомните как одно действие.

С предикатом — `remove_if`:

```cpp
v.erase(
    std::remove_if(v.begin(), v.end(),
                   [](int x) { return x < 0; }),
    v.end()
);
```

### reverse и rotate

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};

std::reverse(v.begin(), v.end());
// v = {5, 4, 3, 2, 1}

std::rotate(v.begin(), v.begin() + 2, v.end());
// «провернуть» так, чтобы элемент с позиции 2 стал началом
```

### sort и связанные

Главный алгоритм сортировки. Использует introsort (комбинацию quicksort, heapsort и insertion sort), O(N log N) в среднем и в худшем случае.

```cpp
std::vector<int> v = {5, 2, 4, 1, 3};

std::sort(v.begin(), v.end());
// v = {1, 2, 3, 4, 5}

// По убыванию через компаратор:
std::sort(v.begin(), v.end(),
          [](int a, int b) { return a > b; });
// v = {5, 4, 3, 2, 1}

// Сортировка строк по длине:
std::vector<std::string> words = {"apple", "hi", "world", "a"};
std::sort(words.begin(), words.end(),
          [](const std::string& a, const std::string& b) {
              return a.size() < b.size();
          });
// words = {"a", "hi", "apple", "world"}
```

Компаратор — это **двухаргументная функция**, возвращающая `true`, если первый «меньше» второго.

`std::stable_sort` — то же, но сохраняет относительный порядок равных элементов. Чуть медленнее, но иногда важно.

`std::partial_sort` — отсортировать только первые N.

`std::nth_element` — поставить N-й элемент на нужное место (как если бы массив был отсортирован). Быстрее, чем полная сортировка, если нужен только медиана или N-й.

### unique

Убрать **подряд идущие** дубликаты:

```cpp
std::vector<int> v = {1, 1, 2, 2, 3, 1, 1};

auto new_end = std::unique(v.begin(), v.end());
v.erase(new_end, v.end());
// v = {1, 2, 3, 1}    ← одиночка 1 в конце осталась!
```

Чтобы убрать **все** дубликаты, сначала отсортируйте:

```cpp
std::vector<int> v = {3, 1, 2, 1, 3, 2};
std::sort(v.begin(), v.end());
// v = {1, 1, 2, 2, 3, 3}
v.erase(std::unique(v.begin(), v.end()), v.end());
// v = {1, 2, 3}
```

## Численные алгоритмы

Из заголовка `<numeric>`:

### accumulate

Свернуть в одно значение:

```cpp
#include <numeric>

std::vector<int> v = {1, 2, 3, 4, 5};
int sum = std::accumulate(v.begin(), v.end(), 0);
// 15
```

Третий аргумент — начальное значение. Тип результата равен типу начального значения. Поэтому осторожно:

```cpp
std::vector<double> v = {1.5, 2.5, 3.5};
auto bad = std::accumulate(v.begin(), v.end(), 0);     // int! результат 6, не 7.5
auto good = std::accumulate(v.begin(), v.end(), 0.0);   // double! 7.5
```

Можно задать **операцию**:

```cpp
// Произведение
int product = std::accumulate(v.begin(), v.end(), 1,
                              std::multiplies<int>());
// 120 (= 1*2*3*4*5)

// Через лямбду
int sum_of_squares = std::accumulate(v.begin(), v.end(), 0,
                                      [](int acc, int x) {
                                          return acc + x * x;
                                      });
// 55 (= 1 + 4 + 9 + 16 + 25)
```

В общем `accumulate(first, last, init, op)` делает: начать с `init`, для каждого элемента `e` сделать `result = op(result, e)`. Это **fold**, тот самый из функциональных языков.

### inner_product

Скалярное произведение двух последовательностей:

```cpp
std::vector<int> a = {1, 2, 3};
std::vector<int> b = {4, 5, 6};
int dot = std::inner_product(a.begin(), a.end(), b.begin(), 0);
// 32 (= 1*4 + 2*5 + 3*6)
```

### partial_sum

Префиксная сумма:

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
std::vector<int> sums(v.size());
std::partial_sum(v.begin(), v.end(), sums.begin());
// sums = {1, 3, 6, 10, 15}
```

### adjacent_difference

Обратная — разности соседних:

```cpp
std::vector<int> sums = {1, 3, 6, 10, 15};
std::vector<int> diffs(sums.size());
std::adjacent_difference(sums.begin(), sums.end(), diffs.begin());
// diffs = {1, 2, 3, 4, 5}
```

## Перестановки

```cpp
std::vector<int> v = {1, 2, 3};

do {
    for (int x : v) std::cout << x;
    std::cout << "\n";
} while (std::next_permutation(v.begin(), v.end()));
```

Выведет все 6 перестановок. Удобно для перебора всех вариантов.

## Бинарный поиск

На **отсортированных** диапазонах:

```cpp
std::vector<int> v = {1, 3, 5, 7, 9, 11};

bool present = std::binary_search(v.begin(), v.end(), 7);   // true

auto it = std::lower_bound(v.begin(), v.end(), 6);   // первый >= 6 → итератор на 7
auto it2 = std::upper_bound(v.begin(), v.end(), 7);   // первый > 7  → итератор на 9
```

O(log N). На несортированных данных результат непредсказуем.

## Лямбды и предикаты — короткое введение

Я уже много раз писал лямбды. Скажу о них минимум, чтобы освоиться. Подробно — в главе 21.

**Лямбда** — это безымянная функция, объявленная «на месте»:

```cpp
auto is_even = [](int x) { return x % 2 == 0; };
std::cout << is_even(4);   // 1
std::cout << is_even(5);   // 0
```

Синтаксис: `[capture](параметры) { тело }`. Возвращаемый тип компилятор выводит сам.

`[ ]` — пустой **захват** (capture). Лямбда может «захватить» переменные из окружения:

```cpp
int threshold = 10;
auto big_enough = [threshold](int x) { return x > threshold; };

std::vector<int> v = {3, 12, 7, 15, 1};
int count = std::count_if(v.begin(), v.end(), big_enough);
// 2 (12 и 15)
```

`[threshold]` — копировать `threshold` в лямбду по значению.
`[&threshold]` — ссылка на оригинал (если изменится — лямбда увидит).
`[=]` — копировать все используемые переменные.
`[&]` — все по ссылке.

Это короткие безымянные функции, идеально подходящие для алгоритмов. Подробнее — глава 21.

### Функтор — старый стиль

До C++11 вместо лямбд использовали **функторы** — классы с перегруженным `operator()`:

```cpp
struct IsBigger {
    int threshold;
    IsBigger(int t) : threshold(t) {}
    bool operator()(int x) const { return x > threshold; }
};

IsBigger pred(10);
int count = std::count_if(v.begin(), v.end(), pred);
```

В современном коде функторы редкость, лямбды короче. Но в чужом коде встречаются.

## Алгоритмы vs методы контейнеров

Иногда один и тот же алгоритм есть и как свободная функция, и как метод контейнера. **Какой брать?**

Правило: **если у контейнера есть свой метод — используйте его**. Он знает структуру и работает быстрее.

Примеры:

| Алгоритм | Свободная функция (медленнее) | Метод контейнера (быстрее) |
|----------|-------------------------------|----------------------------|
| Сортировка list | не работает | `l.sort()` |
| Поиск в map | `std::find` — O(N) | `m.find(key)` — O(log N) |
| Поиск в unordered_map | `std::find` — O(N) | `m.find(key)` — O(1) |
| Поиск в set | `std::find` — O(N) | `s.find(value)` — O(log N) |

`std::find` для `vector`/`array` — нормально, O(N) всё равно. Для `map`/`set` — глупо, у них свой `find` использует дерево.

## Удалить элемент из map во время прохода

Хитрая операция:

```cpp
std::map<std::string, int> m = {...};

// Так нельзя — итератор инвалидируется при erase
for (auto it = m.begin(); it != m.end(); ++it) {
    if (it->second < 0) {
        m.erase(it);   // UB: it после этого мёртв
    }
}

// Правильно — `erase` возвращает следующий итератор
for (auto it = m.begin(); it != m.end(); ) {
    if (it->second < 0) {
        it = m.erase(it);    // двигаемся через возврат
    } else {
        ++it;
    }
}
```

Для `vector` — лучше через erase-remove идиому.

## Свой for_each — попытка номер один

Зная итераторы и лямбды, можем написать свой простой `for_each`:

```cpp
template <typename Iterator, typename Function>
void my_for_each(Iterator first, Iterator last, Function f) {
    for (; first != last; ++first) {
        f(*first);
    }
}

int main() {
    std::vector<int> v = {1, 2, 3, 4};
    my_for_each(v.begin(), v.end(),
                [](int x) { std::cout << x << " "; });
    // 1 2 3 4
}
```

Шаблон обобщает тип итератора и функции. Алгоритмы STL устроены примерно так же, только сложнее (есть проверки, оптимизации, специализации).

## Производительность

Несколько практических замечаний.

### Алгоритмы STL часто оптимизированнее ручных

Реализация `std::sort` в современных компиляторах — десятки тысяч строк, с микро-оптимизациями под архитектуру. Ваш ручной quicksort скорее всего будет в 2-5 раз медленнее.

Поэтому: **используйте STL для очевидно стандартных задач**. Не переписывайте `sort` или `find_if`, если нет особых причин.

### Алгоритмы вызывают функцию для каждого элемента

В цикле:

```cpp
for (int x : v) {
    process(x);
}
```

`process` вызывается на каждый элемент — нет оверхеда. Через `std::for_each`:

```cpp
std::for_each(v.begin(), v.end(), [](int x) { process(x); });
```

Лямбда тоже вызывается на каждый элемент, плюс лямбда создаётся как объект. Компилятор обычно инлайнит, и оверхед нулевой. Но в общем — не страшно.

### Когда STL медленнее ручного

Очень редко. Бывает в случаях:
- Хочется ранний выход из алгоритма, которого нет в STL.
- Особая структура данных, где STL не знает.
- Нужно несколько преобразований за один проход (STL делает их за несколько).

В 99% случаев STL — не узкое место.

## Большой пример: статистика оценок

Собирающий пример. У нас оценки учеников; нужно посчитать среднее, медиану, отклонение, найти лучших, отсортировать.

```cpp
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <string>

struct Student {
    std::string name;
    int score;
};

double average(const std::vector<int>& scores) {
    if (scores.empty()) return 0.0;
    double sum = std::accumulate(scores.begin(), scores.end(), 0.0);
    return sum / scores.size();
}

double median(std::vector<int> scores) {   // копия — модифицируем
    if (scores.empty()) return 0.0;
    std::sort(scores.begin(), scores.end());
    size_t n = scores.size();
    if (n % 2 == 1) {
        return scores[n / 2];
    } else {
        return (scores[n / 2 - 1] + scores[n / 2]) / 2.0;
    }
}

double stddev(const std::vector<int>& scores) {
    if (scores.size() < 2) return 0.0;
    double mean = average(scores);
    double sum_sq = std::accumulate(scores.begin(), scores.end(), 0.0,
                                     [mean](double acc, int x) {
                                         double diff = x - mean;
                                         return acc + diff * diff;
                                     });
    return std::sqrt(sum_sq / (scores.size() - 1));
}

int main() {
    std::vector<Student> students = {
        {"Alice",   85},
        {"Bob",     72},
        {"Charlie", 90},
        {"Dave",    65},
        {"Eve",     78},
        {"Frank",   95},
        {"Grace",   58},
    };
    
    // Выдернем оценки в отдельный вектор
    std::vector<int> scores(students.size());
    std::transform(students.begin(), students.end(),
                   scores.begin(),
                   [](const Student& s) { return s.score; });
    
    std::cout << "Учеников: " << students.size() << "\n";
    std::cout << "Среднее: " << average(scores) << "\n";
    std::cout << "Медиана: " << median(scores) << "\n";
    std::cout << "Ст. отклонение: " << stddev(scores) << "\n";
    
    auto max_it = std::max_element(scores.begin(), scores.end());
    auto min_it = std::min_element(scores.begin(), scores.end());
    std::cout << "Лучшая оценка: " << *max_it << "\n";
    std::cout << "Худшая: " << *min_it << "\n";
    
    // Сколько прошло порог 70
    int passed = std::count_if(scores.begin(), scores.end(),
                                [](int s) { return s >= 70; });
    std::cout << "Прошли (>=70): " << passed << "\n";
    
    // Отсортированный по убыванию рейтинг
    std::sort(students.begin(), students.end(),
              [](const Student& a, const Student& b) {
                  return a.score > b.score;
              });
    
    std::cout << "\nРейтинг:\n";
    int rank = 1;
    for (const auto& s : students) {
        std::cout << rank++ << ". " << s.name << " (" << s.score << ")\n";
    }
    
    return 0;
}
```

Соберите:

```bash
$ g++ -std=c++11 -Wall -Wextra stats.cpp -o stats
$ ./stats
```

Использовано: `accumulate` (с лямбдой для квадратов отклонений), `transform` (выдернуть поле из структур), `min_element`/`max_element`, `count_if`, `sort` с лямбдой-компаратором.

Это **идиоматичный** современный C++. Никаких ручных циклов для поиска max/min, для подсчёта, для сортировки. Все стандартное.

## Главные правила

1. **Для известной задачи — берите алгоритм STL**. Не пишите свой `sort` или `find`.
2. **Для `map`/`set`/`unordered_*` — `find` метод контейнера**, не `std::find`.
3. **`erase-remove`** для удаления из вектора по предикату.
4. **`accumulate` с типом начального значения** — `0` для `int`, `0.0` для `double`.
5. **Лямбды с захватом по ссылке (`[&]`)** — только когда понимаете, что переменная переживёт лямбду.
6. **На отсортированных** — бинарный поиск (`binary_search`, `lower_bound`).
7. **Помните инвалидацию итераторов** при `erase` — используйте возвращаемое значение.

## Маленькое упражнение

1. Дан `std::vector<int>`. Найдите сумму всех **положительных** элементов. Используйте `accumulate` с лямбдой.

2. Дан `std::vector<std::string>`. Отсортируйте по длине, при равных длинах — лексикографически.

3. Дан `std::vector<int>`. Используя `transform`, постройте новый вектор, где каждый элемент — квадрат исходного.

4. Дан `std::vector<int>`. Используя `partial_sum`, постройте префиксные суммы. Используя их, ответьте на запрос «сумма от индекса L до R включительно» за O(1).

5. Дан `std::vector<std::pair<std::string, int>>` — оценки. Найдите имя ученика с максимальной оценкой через `max_element` с компаратором.

6. (Сложнее) Дан `std::vector<int>`. Сделайте функцию, которая удаляет все дубликаты, но **сохраняет первоначальный порядок** (то есть `sort` + `unique` не подходит).

## Часть I закрыта

На этом Часть I заканчивается. Перечислю кратко, что у вас теперь есть:

- **Глава 8**: типы и переменные. `int`, `double`, `bool`, `char`, `const`, `constexpr`, `auto`.
- **Глава 9**: управляющие конструкции. `if/else`, `switch`, `while`, `for`, range-based for.
- **Глава 10**: функции. Объявление/определение, передача по значению/ссылке/указателю, перегрузка, рекурсия.
- **Глава 11**: память. Стек/куча, `new`/`delete`, RAII — главный паттерн C++.
- **Глава 12**: строки и I/O. `std::string`, `cin`/`cout`/файловые потоки, `stringstream`.
- **Глава 13**: контейнеры. `vector`, `array`, `list`, `map`, `unordered_map`, `set`.
- **Глава 14**: алгоритмы и итераторы. `find`, `sort`, `transform`, `accumulate`, лямбды.

Этого хватит, чтобы писать осмысленные программы. Но это всё ещё «низший уровень» C++ — мы пока не делали свои классы, не пользовались наследованием, не работали с умными указателями. Это всё в Части II.

## Что дальше

**Часть II — RPG-проект `demo-rpg/`**. Мы начинаем писать большую программу: текстовая игра с локациями, инвентарём, NPC, боями, сохранениями. Все знания Части I заработают вместе, плюс мы введём:
- Классы и объекты (глава 16).
- Наследование и виртуальные функции (глава 17).
- Smart pointers `unique_ptr`/`shared_ptr`/`weak_ptr` (глава 18).
- Move-семантику и rvalue-ссылки (глава 19).
- Шаблоны функций и классов (глава 20).
- Лямбды и `std::function` (глава 21) — после кратких намёков.
- Исключения и exception safety (глава 22).
- Парсер скриптов локаций (глава 23).
- Сохранение/загрузку (глава 24).

Сначала — глава 15: дизайн игры и скелет проекта. Мы разработаем архитектуру, создадим файловую структуру `demo-rpg/`, напишем Makefile с автогенерацией зависимостей, и подготовим почву для следующих глав. Это будет существенно отличаться от глав «учебных примеров»: реальная разработка с реальными решениями.
