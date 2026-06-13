# Глава 41. Многопоточность — std::thread, mutex, condition_variable

Наш echo-server из главы 40 обслуживает **одного клиента за раз**. Пока один общается — другие висят в очереди. Это неприемлемо для чата: пользователи должны переписываться **одновременно**.

Решение — **один поток на клиента**. Server-loop принимает соединения, для каждого forks-новый std::thread. Все потоки работают параллельно, читают свои сокеты, общаются с общими данными (списком клиентов).

В этой главе изучим инструменты C++11 для многопоточности: `std::thread`, `std::mutex`, `std::lock_guard`, `std::condition_variable`. И сразу применим — превратим echo-server в **multi-client чат с broadcast**: что один сказал, все услышали.

## Что такое поток

**Поток** (thread) — это исполняющаяся последовательность инструкций внутри процесса. У потоков общая память процесса, но отдельные стеки. ОС переключает процессор между потоками — на 8 ядрах могут реально выполняться 8 потоков параллельно.

Concurrent vs parallel:
- **Concurrent** — много вещей идут «одновременно» с точки зрения логики. На одноядерном CPU тоже concurrent (через переключение).
- **Parallel** — реально одновременно на нескольких CPU.

C++11 ввёл стандартную поддержку. До этого — POSIX `pthread_*`, Windows `CreateThread`. Сейчас — `std::thread`, переносимо.

## std::thread

```cpp
#include <thread>
#include <iostream>

void worker(int id) {
    std::cout << "thread " << id << " работает\n";
}

int main() {
    std::thread t(worker, 42);
    t.join();   // ждать завершения
}
```

`std::thread t(func, args...)` — создать поток, запустить `func(args...)` параллельно.

`t.join()` — заблокировать текущий поток до завершения `t`. Аналог `waitpid` для процессов.

`t.detach()` — **«забыть»** поток. Он работает в фоне, мы за ним не следим. Когда программа завершается, detached-потоки тоже умирают (как daemon threads в Java).

```cpp
std::thread(worker, 42).detach();   // fire-and-forget
```

Это то, что мы будем использовать для chat-server: на каждого клиента — поток, который сам уберётся при отключении клиента.

**Опасность detach**: если поток обращается к стеку main'a, который уже разрушен — UB. У нас потоки работают со своим аргументом (fd) и глобальными данными — безопасно.

### Что захватывает std::thread

`std::thread(func, arg)` **копирует** или **перемещает** аргументы в свой контекст. Если хотите передать по ссылке — `std::ref(x)`:

```cpp
int counter = 0;
std::thread t(increment, std::ref(counter));   // counter передан по ссылке
```

Лямбды тоже работают:

```cpp
std::thread t([]() { ... });
std::thread t2([n = 5]() { ... });   // capture
```

## Race condition

Два потока меняют **одну переменную** — может быть гонка:

```cpp
int counter = 0;

void inc() {
    for (int i = 0; i < 1000000; ++i) ++counter;
}

int main() {
    std::thread a(inc), b(inc);
    a.join(); b.join();
    std::cout << counter;   // ??? Ожидаемо 2000000, реально меньше
}
```

`++counter` это **три** машинных инструкции: read, increment, store. Между read одного потока и store другого — другой поток может тоже сделать read+inc+store. Часть инкрементов **теряется**.

Это **data race** — UB по стандарту C++. Программа может вернуть что угодно.

Защита: либо **мьютекс** (lock), либо **atomic** (специальные типы — глава 42).

## std::mutex

`std::mutex` (mutual exclusion) — синхронизационный объект. В каждый момент его держит **не больше одного** потока. Остальные ждут.

```cpp
#include <mutex>

int counter = 0;
std::mutex m;

void inc() {
    for (int i = 0; i < 1000000; ++i) {
        m.lock();
        ++counter;
        m.unlock();
    }
}
```

Теперь только один поток в любой момент в `++counter`. Race нет.

**Проблема `lock`/`unlock` вручную**: если `++counter` бросит исключение — `unlock` не выполнится, мьютекс **навсегда залочен**, другие потоки висят. Это deadlock.

### lock_guard — RAII

Решение — `std::lock_guard`:

```cpp
void inc() {
    for (int i = 0; i < 1000000; ++i) {
        std::lock_guard<std::mutex> lock(m);
        ++counter;
    }
}
```

`lock_guard` в конструкторе **захватывает** мьютекс, в деструкторе **отпускает**. Это **RAII** (глава 11). При любом выходе из scope — исключении, return, break — `unlock` гарантированно срабатывает.

В современном C++ **никогда не пишите** `lock()`/`unlock()` руками. Всегда `lock_guard` или `unique_lock`.

В C++17 — `std::scoped_lock`, может лочить несколько мьютексов сразу без deadlock.

### unique_lock

`std::unique_lock` — более гибкий вариант:

```cpp
std::unique_lock<std::mutex> lock(m);
// что-то делаем
lock.unlock();   // явный unlock
// что-то ещё без lock
lock.lock();     // снова взять
```

Может временно отпустить мьютекс. Нужен для `condition_variable` (ниже).

`lock_guard` проще и быстрее. Используйте его, кроме случаев когда нужна гибкость.

## Atomicity vs locking

`std::atomic<int>` — special тип, **атомарные** операции на нём гарантированы:

```cpp
#include <atomic>
std::atomic<int> counter{0};

void inc() {
    for (int i = 0; i < 1000000; ++i) ++counter;
}
```

Никакого `lock_guard`. `++counter` под капотом — атомарная инструкция процессора (на x86 — `LOCK XADD`).

**Atomic** быстрее **mutex** для простых операций (один счётчик). Mutex нужен для **сложных** критических секций (несколько связанных переменных).

Подробно про atomic — глава 42.

## condition_variable

Иногда поток должен **ждать** условия. Например, worker ждёт, пока появится задача в очереди.

`std::mutex` решает «не наступайте друг другу на пятки», но **не** «дождись, пока что-то произойдёт».

`std::condition_variable`:

```cpp
std::mutex m;
std::condition_variable cv;
std::queue<Task> tasks;
bool stop = false;

void worker() {
    while (true) {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&]() { return stop || !tasks.empty(); });
        if (stop && tasks.empty()) return;
        Task t = std::move(tasks.front());
        tasks.pop();
        lock.unlock();
        t.run();
    }
}

void enqueue(Task t) {
    {
        std::lock_guard<std::mutex> lock(m);
        tasks.push(std::move(t));
    }
    cv.notify_one();
}
```

`cv.wait(lock, predicate)`:
1. Проверяет `predicate`. Если true — продолжает.
2. Иначе — **отпускает мьютекс** и засыпает.
3. Кто-то вызывает `cv.notify_one()` — поток просыпается, **берёт мьютекс обратно**, проверяет предикат, повторяет если false.

`notify_one()` — разбудить **один** ждущий поток. `notify_all()` — все.

Это **producer-consumer pattern**. Producer вызывает `enqueue`, consumer — `worker`. Без `condition_variable` worker бы делал busy-wait (постоянная проверка), нагружая CPU.

## Deadlock

Самая страшная проблема многопоточности. Поток A держит мьютекс X, ждёт Y. Поток B держит Y, ждёт X. **Оба висят навсегда.**

```cpp
std::mutex mx, my;

void a() {
    std::lock_guard<std::mutex> la(mx);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> lb(my);   // ждём my
}

void b() {
    std::lock_guard<std::mutex> lb(my);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> la(mx);   // ждём mx
}
```

Запустите параллельно — deadlock.

### Профилактика

1. **Всегда брать мьютексы в одном порядке.** Сначала `mx`, потом `my`. Если все потоки соблюдают — deadlock невозможен.

2. **`std::scoped_lock`** (C++17) — берёт несколько мьютексов **атомарно** через специальный алгоритм:
   ```cpp
   std::scoped_lock lock(mx, my);
   ```
   Безопасно.

3. **`std::lock(mx, my)`** в C++11 — то же:
   ```cpp
   std::lock(mx, my);
   std::lock_guard<std::mutex> la(mx, std::adopt_lock);
   std::lock_guard<std::mutex> lb(my, std::adopt_lock);
   ```
   Громоздко, в C++17 заменено scoped_lock.

4. **Hierarchy lock** — нумеруйте мьютексы, берите только в возрастающем порядке.

В нашем chat-server **один мьютекс** на список клиентов — deadlock невозможен.

## Многопоточный chat-server

Идея проста:

```
main loop:
    accept() → c
    std::thread(handle_client, c).detach();
```

Каждый клиент в своём потоке.

Shared state: **список клиентов**. Защищается мьютексом.

```cpp
struct Client {
    int fd;
    std::string name;
};

std::mutex g_mutex;
std::vector<Client> g_clients;
```

### broadcast

При получении сообщения от одного клиента — посылаем всем остальным:

```cpp
void broadcast(int from_fd, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& c : g_clients) {
        if (c.fd == from_fd) continue;
        ::send(c.fd, msg.data(), msg.size(), MSG_NOSIGNAL);
    }
}
```

Лочим мьютекс на время рассылки. Без лока — другой поток мог бы изменять список во время итерации (UB на vector).

**`MSG_NOSIGNAL`** — флаг для `send`: не посылать SIGPIPE если другой конец закрыт. Без него, если один клиент отключился, наш сервер бы получил SIGPIPE и упал.

**Внимание**: `send` блокирует, если буфер receiver'а полный (медленный клиент). В таком случае **весь broadcast** заморожен — лочка не отдаётся, другие клиенты тоже ждут.

Реальные чат-серверы решают это через **per-client queue + writer thread**, или через **non-blocking send + epoll**. У нас простая версия — работает на быстрых клиентах.

### handle_client

```cpp
void handle_client(int fd) {
    std::string name = "user_" + std::to_string(g_next_id++);
    register_client(fd, name);

    broadcast(fd, "* " + name + " вошёл в чат\n");

    char buf[1024];
    std::string acc;
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; break; }
        acc.append(buf, n);

        // Разбиваем по '\n'
        std::size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            if (line == "/quit") goto disconnect;
            broadcast(fd, "<" + name + "> " + line + "\n");
        }
    }
disconnect:
    unregister_client(fd);
    ::close(fd);
    broadcast(-1, "* " + name + " вышел из чата\n");
}
```

Каждый поток:
1. Регистрирует своего клиента в общем списке.
2. Шлёт всем «вошёл».
3. Циклически читает, накапливает в `acc` (важно — TCP это поток, сообщение может прийти частями), разбивает по `\n`, broadcast'ит.
4. На /quit или close — unregister и «вышел».

**`acc`** — accumulator для частичных recv. Если клиент написал «`hello\nworld\n`», но второй recv получил `"hel"` и `"lo\nworld\n"` отдельно — мы корректно собираем сообщения.

**`g_next_id++`** — race condition! Два клиента подключились одновременно, оба `++` — могут получить одинаковый id. Защита: атомик или мьютекс. Для учебной цели игнорируем (вероятность мала).

### main

```cpp
int main() {
    // ... socket/bind/listen ...
    while (true) {
        int c = ::accept(srv, ...);
        std::thread(handle_client, c).detach();
    }
}
```

Просто. Каждый `accept` → новый поток.

`-pthread` обязательно при сборке (Linux) — линковка с pthread library. На macOS обычно автоматом.

## Запуск

Терминал 1:
```bash
$ ./build/threaded_chat_server 9099
Chat-server слушает на :9099
[server] user_1 присоединился
[server] <user_1> привет всем
[server] user_2 присоединился
[server] <user_2> привет user_1
```

Терминал 2 (Alice):
```bash
$ ./build/echo_client 127.0.0.1 9099
> <- Привет, user_1. Пишите сообщения. /quit для выхода.
привет всем
> <- * user_2 вошёл в чат
<- <user_2> привет user_1
```

Терминал 3 (Bob):
```bash
$ ./build/echo_client 127.0.0.1 9099
> <- Привет, user_2. Пишите сообщения. /quit для выхода.
> <- <user_1> привет всем          (увидел уже отправленное Alice)
```

(Echo client лёгкое неудобство — он не отделяет отправку от приёма; реальный chat-client сделаем позже.)

## Архитектура «thread-per-connection»

Что мы реализовали — **thread-per-connection** модель. Один поток обслуживает одного клиента всю его сессию.

**Плюсы**:
- Простой код. Сериальная логика «recv → process → broadcast → recv».
- Изоляция: один клиент не мешает другому (если не блокирует общий мьютекс).

**Минусы**:
- **Память**: каждый поток имеет свой стек, обычно 8 MB на Linux. 10 000 клиентов = 80 GB. Серьёзная нагрузка.
- **CPU**: переключение контекста между потоками — микросекунды. На тысячах активных потоков — заметно.
- **GIL-подобные проблемы**: общий мьютекс становится bottleneck.

Альтернатива — **реактор** на `epoll`/`kqueue`: **один поток** обслуживает тысячи соединений через non-blocking IO. Глава 43.

В production:
- Nginx — реактор + worker-процессы.
- Apache (старая модель) — thread/process per connection. Сейчас тоже event-based.
- Node.js, Redis, MongoDB — реактор.

## Главные правила главы

1. **`std::thread`** + `join()` или `detach()` — основа потоков.
2. **`std::lock_guard<std::mutex>`** в RAII-стиле — никогда `lock`/`unlock` руками.
3. **`std::condition_variable`** для wait/notify, не busy-wait.
4. **Atomic для простых счётчиков**, mutex для составных данных.
5. **Deadlock = circular wait.** Берите мьютексы в одном порядке или через `scoped_lock`.
6. **`detach`** для fire-and-forget. Не обращайтесь к стеку родителя.
7. **`MSG_NOSIGNAL`** на serverside `send` — против SIGPIPE.
8. **Thread-per-connection** простая модель; реактор для масштаба.

## Маленькое упражнение

1. Соберите и запустите сервер. Подключите 2-3 клиента, обменяйтесь сообщениями.

2. Закомментируйте `lock_guard` в broadcast. Запустите. На большой нагрузке (много клиентов отправляют одновременно) — увидите крэш через vector iteration.

3. Замените `++g_next_id` на `std::atomic<int> g_next_id{1}; g_next_id.fetch_add(1)`. Это безопаснее.

4. Реализуйте команду `/who` — клиент отправляет, получает список всех в чате.

5. Реализуйте команду `/nick <name>` — клиент меняет имя.

6. (Сложнее) Реализуйте **rooms**: команда `/join general` помещает клиента в комнату. Broadcast только в той же комнате.

7. (Сложнее) Используйте **thread pool** вместо thread-per-connection: фиксированное число worker-потоков и очередь задач.

8. (Очень сложно) Замените `send` в broadcast на **per-client queue + writer thread**. Тогда медленный клиент не блокирует broadcast.

## Что дальше

Глава 42 — **`std::atomic` и memory ordering**. Что значит `relaxed`/`acquire`/`release`/`seq_cst`. Lock-free queue. Compare-and-swap.

Дальше — реактор (43), бинарный протокол (44), многокомнатный чат (45), шифрование+история (46), деплой (47).
