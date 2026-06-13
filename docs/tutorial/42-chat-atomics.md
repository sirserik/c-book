# Глава 42. std::atomic и memory ordering

В прошлой главе мы использовали `std::mutex` для защиты shared state. Но для **простых** операций (один счётчик, один флаг) mutex — overkill. Каждый `lock`/`unlock` — это syscall в худшем случае, переключение контекста, кэш-инвалидация.

**`std::atomic`** даёт **lock-free** примитивы: операции, которые **атомарны** на уровне процессора. Без мьютекса. В десятки раз быстрее на простых случаях.

Но за скорость — цена сложности. `std::atomic` приносит понятие **memory ordering** — `relaxed`/`acquire`/`release`/`seq_cst`. Это одна из самых сложных тем C++. В этой главе разберём её на пальцах.

## Зачем atomic — наглядно

Запустим демо:

```
$ ./build/atomic_demo
Без защиты:        counter=1248361 (ожидалось 2000000), 3.93 ms
Mutex:             counter=2000000, 101.99 ms
atomic (seq_cst):  counter=2000000, 34.85 ms
atomic (relaxed):  counter=2000000, 43.99 ms
```

Два потока инкрементируют один счётчик миллион раз каждый. Ожидаемо 2 000 000.

**Без защиты**: получили 1 248 361 — **38% инкрементов потерялись** из-за race condition. Зато быстро (4 ms).

**Mutex**: правильный результат, 102 ms.

**atomic с seq_cst**: правильный результат, 35 ms. **В 3 раза быстрее mutex**.

**atomic с relaxed**: правильный результат, 44 ms (в этом тесте — почти как seq_cst, но на других нагрузках разница может быть в разы).

Atomic выигрывает по двум причинам: **нет syscall** (mutex иногда зовёт ОС для парковки потока) и **нет переключений контекста**. Это «спин на CPU instruction»: процессор использует особые atomic-инструкции (`LOCK XADD` на x86, `LDXR/STXR` на ARM).

## std::atomic

```cpp
#include <atomic>

std::atomic<int> counter{0};

++counter;                              // атомарный инкремент
counter.fetch_add(1);                   // явный
counter += 5;                            // тоже атомарно
counter.store(42);                       // присвоить
int v = counter.load();                  // прочитать
```

Все эти операции **атомарны**: либо вся применилась, либо нет. Никакого «частично применённого» состояния, как с обычным `++`.

Для каких типов работает:
- **Встроенные числа** (`int`, `long`, `uint64_t`, ...): всегда атомарны.
- **Указатели** (`std::atomic<T*>`): атомарны.
- **Bool** (`std::atomic<bool>`): атомарен.
- **Свои struct**: если их размер ≤ 8 байт и trivially copyable — атомарны через `cmpxchg`. Иначе — внутри будет мьютекс.

Проверить, lock-free ли тип на вашей платформе:

```cpp
if (std::atomic<int>::is_always_lock_free) {
    // на этой платформе int atomic = реально lock-free
}
```

## compare_exchange (CAS)

Самая мощная атомарная операция — **compare-and-swap (CAS)**:

```cpp
bool compare_exchange_strong(T& expected, T desired);
```

Логика:
1. Прочитать текущее значение атомика.
2. Если оно равно `expected` — заменить на `desired`, вернуть `true`.
3. Иначе — записать **текущее** значение в `expected`, вернуть `false`.

**Всё это — атомарно**.

Использование — типичный паттерн для lock-free алгоритмов:

```cpp
std::atomic<int> value{0};

int expected = value.load();
int desired;
do {
    desired = compute(expected);
} while (!value.compare_exchange_weak(expected, desired));
```

«Прочитал, посчитал новое, попробовал поменять — если кто-то опередил, начну заново».

**weak vs strong**: `compare_exchange_weak` может **ложно** возвращать `false` (на некоторых архитектурах для производительности). В цикле — нормально. Вне цикла — используйте `strong`.

## Memory ordering — главная страшилка

Здесь и начинается сложное.

В мире однопоточной программы всё последовательно: «инструкция A, потом B, потом C». В мире многопоточной — **не так**. Процессор может **переставить** инструкции, компилятор тоже может, кэши разных ядер видят значения по-разному.

Пример:

```cpp
// Thread 1                  Thread 2
data = 42;                   while (!ready) {}
ready = true;                std::cout << data;
```

Что напечатает Thread 2? Кажется, 42. На самом деле — **может напечатать 0**. Почему?

- Компилятор может переставить присваивания в Thread 1 (если не видит зависимости): `ready = true; data = 42;`.
- CPU может выполнить store'ы в другом порядке.
- Кэши могут синхронизироваться с задержкой.

Чтобы это работало, нужен **memory barrier** — гарантия: «всё, что было до этого store, видно после load на другом ядре».

В C++11 эти барьеры задаются через **memory_order**:

```cpp
counter.fetch_add(1, std::memory_order_relaxed);
counter.store(42, std::memory_order_release);
int v = counter.load(std::memory_order_acquire);
```

## Пять режимов memory_order

### memory_order_relaxed

«Атомарность есть, но **никаких** гарантий порядка с другими операциями».

```cpp
std::atomic<int> c{0};
c.fetch_add(1, std::memory_order_relaxed);
```

Самый быстрый. Подходит когда:
- Просто счётчик, и его значение нужно только в конце.
- Не используем как **сигнал** между потоками.

Пример: счётчик `processed_requests`. Каждый поток инкрементирует. В конце программы читаем суммарное. Никаких других операций «после inc» не нужно — `relaxed` ок.

### memory_order_release / memory_order_acquire

Парная гарантия. **release** на write, **acquire** на read.

```cpp
// Producer
data = 42;
ready.store(true, std::memory_order_release);   // всё что было ДО store видно

// Consumer
while (!ready.load(std::memory_order_acquire)) {}   // увидит данные ДО store
std::cout << data;   // напечатает 42 (или старше; не «новее» чем release)
```

`release` говорит: «все мои предыдущие операции должны быть **завершены** до этого store».

`acquire` говорит: «все мои последующие операции должны видеть всё, что было перед matching release».

Это даёт **synchronizes-with** отношение. Самая частая пара в lock-free коде.

### memory_order_acq_rel

Когда **одна** операция и читает, и пишет (RMW = read-modify-write): `fetch_add`, `compare_exchange`. Гарантии acquire (на read часть) + release (на write часть).

### memory_order_seq_cst — sequentially consistent

**Самый строгий**. Все операции в программе **выстраиваются в единый порядок**, видимый всеми потоками. Гарантии acq_rel + общий глобальный порядок.

**Это default для atomic-операций без указания** (`++counter` = `seq_cst`).

Самый медленный. Но **проще рассуждать**: всё последовательно, как однопоточная программа.

В большинстве кода **seq_cst достаточно**. Используйте relaxed/acquire/release только когда **нужна производительность** и вы понимаете, что делаете.

## Сравнение

| Ordering | Атомарность | Sync-with | Скорость | Когда |
|----------|-------------|-----------|----------|-------|
| `relaxed` | ✓ | ✗ | быстрее всего | Счётчики, статистика |
| `release` (только store/RMW) | ✓ | ✓ (с acquire) | средне | Producer: «вот данные готовы» |
| `acquire` (только load/RMW) | ✓ | ✓ (с release) | средне | Consumer: «жду данные» |
| `acq_rel` (только RMW) | ✓ | ✓ обе стороны | средне | CAS-loops |
| `seq_cst` | ✓ | ✓ глобально | медленнее всего | Когда не уверены — берите это |

## Пример с release/acquire

«Producer публикует, consumer ждёт»:

```cpp
struct Message { int data; };
Message msg;
std::atomic<bool> ready{false};

// Producer
void producer() {
    msg.data = 42;                                // обычная запись
    ready.store(true, std::memory_order_release); // публикуем
}

// Consumer
void consumer() {
    while (!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::cout << msg.data;   // гарантированно 42
}
```

`release` создаёт барьер: «всё, что было до store, видно после acquire». В частности, `msg.data = 42` гарантированно видно consumer'у.

Без `release`/`acquire` (например, с relaxed на обе стороны) — нет гарантии. Consumer мог бы увидеть `ready == true` но `data == 0`.

Это **паттерн публикации**: один поток подготавливает данные, другой ждёт сигнала.

## Lock-free очередь SPSC

**Single-Producer/Single-Consumer** очередь — один поток пишет, другой читает. Lock-free реализация — кольцевой буфер с двумя индексами:

```cpp
template <typename T, std::size_t N>
class SpscQueue {
public:
    bool push(const T& v) {
        std::size_t h = head_.load(std::memory_order_relaxed);
        std::size_t next = (h + 1) % N;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // полна
        }
        buf_[h] = v;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return false;  // пуста
        }
        out = buf_[t];
        tail_.store((t + 1) % N, std::memory_order_release);
        return true;
    }

private:
    T buf_[N];
    std::atomic<std::size_t> head_{0};   // producer пишет сюда
    std::atomic<std::size_t> tail_{0};   // consumer читает отсюда
};
```

Логика:
- `head` — индекс, куда **писать** дальше. Меняет producer.
- `tail` — индекс, **откуда** читать. Меняет consumer.
- Очередь пустая когда `head == tail`. Полная когда `(head + 1) % N == tail`.

Memory ordering:
- Producer пишет в `buf_[h]`, потом publish'ит `head` через **release**.
- Consumer читает `head` через **acquire**, чтобы гарантированно увидеть `buf_[h]`.
- Симметрично для `tail`.

`relaxed` на собственных чтениях (own loads) — потому что один поток пишет одну переменную, никакой синхронизации не нужно для **своих** чтений.

Это **классическая** lock-free структура. SPSC — самый простой случай. Multi-producer/multi-consumer (MPMC) — гораздо сложнее, требует Michael-Scott queue или подобных алгоритмов.

## ABA-проблема

Главная ловушка lock-free программирования. Имеем `compare_exchange`:

```cpp
int expected = atom.load();
do {
    int desired = ...;
} while (!atom.compare_exchange_weak(expected, desired));
```

CAS гарантирует «изменилось ли значение с момента моего read». **Но не** «прошло ли через другие значения».

ABA: я прочитал A. Другой поток сделал A → B → A. Я делаю CAS с expected=A — успех. **Но что-то изменилось**, и моя логика может быть неверна.

Пример: lock-free stack. Я хочу popнуть. Прочитал top = node_X. Другой поток popнул X, освободил, выделил новый node_X (тот же адрес), запушил. Мой CAS top == old_X пройдёт, но **внутри — другой объект**.

Решение — **tag** к указателю (старшие биты как «версия»). Или **hazard pointers** / **epoch-based reclamation**.

В нашей мини-СУБД, mini-shell, chat-server — ABA не встретится. Это для глубокой lock-free работы.

## std::memory_order_consume

Шестой режим, но **не используется** в практике. Стандарт оставил, но компиляторы трактуют как `acquire`. Тема для академических исследований.

## Когда использовать atomic, когда mutex

Грубые правила:

**atomic для:**
- Простых счётчиков, флагов.
- Lock-free структур данных (если вам реально нужно).
- Сигнализации между потоками («готов»/«не готов»).

**mutex для:**
- Составных данных (vector, map, struct с несколькими полями).
- Когда логика «прочитать X, проверить, записать Y» должна быть атомарной.
- Условных переменных (`std::condition_variable` требует mutex).

В chat-server мы использовали mutex — список клиентов это составная структура. Правильно.

Если бы делали счётчик `total_messages_sent` — atomic было бы быстрее.

## Lock-free vs wait-free

**Lock-free** — система всегда **прогрессирует** (хотя бы один поток успешно завершит операцию).

**Wait-free** — **каждый** поток гарантированно завершит операцию за конечное число шагов. Сильнее lock-free.

**Blocking** (mutex) — поток может ждать долго (если кто-то держит lock).

Wait-free алгоритмы редки и сложны. Lock-free — реалистичная цель для производительности.

## Главные правила главы

1. **atomic для простого** (счётчики), **mutex для сложного** (составные данные).
2. **Default `seq_cst`** — самый строгий, проще рассуждать. Оптимизация через `relaxed`/`acq_rel` — только когда измеряли и нужно.
3. **release-acquire пара** — главная идиома lock-free публикации.
4. **`compare_exchange_weak` в цикле**, `strong` вне цикла.
5. **`is_always_lock_free`** проверка перед использованием на ваших типах.
6. **ABA-проблема** при работе с указателями в lock-free.
7. **Lock-free не всегда быстрее mutex** — на низкой нагрузке mutex быстрее, на высокой — atomic.
8. **Тестируйте под нагрузкой** — race condition проявляется только при contention.

## Маленькое упражнение

1. Запустите `./build/atomic_demo`. Посмотрите, насколько race теряет инкрементов на вашей машине.

2. Замените в `threaded_chat_server.cpp` `int g_next_id` на `std::atomic<int>`. Используйте `g_next_id.fetch_add(1)`.

3. Напишите минимальный SPSC queue по примеру из главы. Запустите тест: producer-поток отправляет числа 1..N, consumer проверяет, что прочитал все по порядку.

4. (Сложнее) Реализуйте producer-consumer **тест на скорость**: SPSC через atomic vs через mutex. Замерьте throughput.

5. (Сложнее) Сделайте `std::atomic<bool> shutdown_flag` в server. Поток-обработчик клиента проверяет на каждой итерации и выходит. Это «cooperative cancellation».

6. (Очень сложно) Реализуйте MPMC очередь. Подсказка: Michael-Scott queue. Это десятки строк сложного кода.

7. (Очень сложно) Прочитайте «C++ Concurrency in Action» (Anthony Williams) — глава про memory ordering. Самый ясный текст по теме.

8. Используйте `std::atomic_thread_fence` — explicit fence без атомика. Когда полезно?

## Что дальше

Глава 43 — **реактор**: `select`/`poll`/`epoll`/`kqueue`. Один поток, тысячи соединений. Без потока на клиента — gigascale.

Дальше — бинарный протокол (44), многокомнатный чат (45), шифрование+история (46), деплой (47). Часть V на финишной прямой.
