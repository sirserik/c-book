# Глава 43. Реактор — select/poll/epoll/kqueue

В главе 41 мы сделали чат-сервер на **thread-per-connection**. Работает, но не масштабируется: на 10 000 клиентов нужно 10 000 потоков, 80 GB памяти под стеки, deadly context switching.

В этой главе — **реактор**. **Один поток** обслуживает **тысячи** соединений. Принцип: вместо «блокироваться на recv одного клиента» — «опрашивать ОС: на каком из моих fd готовы данные?». Это **event-driven IO**.

Сделаем чат-сервер на `poll()` — кросс-платформенный. На production использовали бы `epoll` (Linux) или `kqueue` (BSD/macOS) — масштабируемые до миллионов соединений. Идея та же.

## C10K problem

В конце 90-х серверы упёрлись в **C10K** — «10 000 одновременных соединений». На thread-per-connection упирались:
- Память: стек 8 MB × 10k = 80 GB.
- CPU: переключение контекстов раз в миллисекунду × 10k = простой.

Решение нашли через **non-blocking IO + event polling**. Один поток, десятки тысяч соединений.

Сейчас обсуждают **C10M** — 10 миллионов. Решения экзотичные: bypass kernel TCP stack (DPDK), kernel bypass entirely (XDP, eBPF), zero-copy IO.

Для обычных приложений `epoll`/`kqueue` хватает на сотни тысяч.

## Blocking vs non-blocking

**Blocking IO** (как в главах 40, 41):
```cpp
ssize_t n = recv(fd, buf, sizeof(buf), 0);   // блокирует до данных
```

Если данных нет — поток спит. ОС просыпает его, когда придут.

**Non-blocking IO**:
```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);            // переключаем сокет в non-blocking режим
ssize_t n = recv(fd, buf, sizeof(buf), 0);   // возвращается сразу
if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // данных нет — пробуйте позже
}
```

`recv` возвращается **немедленно**. Если данные есть — возвращает. Если нет — `-1` с `errno == EAGAIN` (или `EWOULDBLOCK`, синоним).

С non-blocking один поток может **попробовать** все свои сокеты по очереди. Но это **busy-wait** — CPU кипит. Нужно **спать**, пока кто-то не готов.

Для этого — `select`/`poll`/`epoll`/`kqueue`.

## select() — оригинал

POSIX даёт `select`:

```cpp
fd_set readfds;
FD_ZERO(&readfds);
FD_SET(srv, &readfds);
FD_SET(c1, &readfds);
FD_SET(c2, &readfds);

select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
// Заблокирует, пока хоть один fd не готов

if (FD_ISSET(srv, &readfds)) { /* accept */ }
if (FD_ISSET(c1, &readfds))  { /* recv c1 */ }
```

`fd_set` — битовая маска. Ограничение: max fd обычно **1024**. Это слишком мало для серверов.

Плюс — после select **нужно перебрать все fd**, проверить кто готов. O(N) на каждой итерации.

`select` появился в 1983 году. Сейчас почти не используется в production.

## poll() — улучшенный select

```cpp
std::vector<pollfd> pfds;
pfds.push_back({srv, POLLIN, 0});
pfds.push_back({c1, POLLIN, 0});

poll(pfds.data(), pfds.size(), -1 /*ms*/);

if (pfds[0].revents & POLLIN) { /* accept */ }
if (pfds[1].revents & POLLIN) { /* recv */ }
```

`pollfd`:
- `fd` — файловый дескриптор.
- `events` — что нас интересует (`POLLIN`/`POLLOUT`/`POLLERR`/...).
- `revents` — что **реально** произошло (заполняется `poll`).

Преимущества над select:
- **Нет лимита 1024**.
- Передаём вектор, не битовую маску — компактнее на маленьких N.

Но **те же** проблемы:
- O(N) на каждой итерации — ядро перебирает все fd.
- Структура с `events`/`revents` копируется в ядро каждый вызов.

`poll` подходит до **~1000 соединений**. Дальше — медленно.

## epoll (Linux) и kqueue (BSD/macOS)

Современные scalable mechanisms:

**`epoll`** (Linux):
```cpp
int ep = epoll_create1(0);

epoll_event ev;
ev.events = EPOLLIN;
ev.data.fd = srv;
epoll_ctl(ep, EPOLL_CTL_ADD, srv, &ev);

epoll_event events[64];
int n = epoll_wait(ep, events, 64, -1);
for (int i = 0; i < n; ++i) {
    // events[i].data.fd готов
}
```

Идея: **регистрируем** интересующие fd один раз (`epoll_ctl`), потом многократно вызываем `epoll_wait`. Ядро **запоминает** список, не перебирает каждый раз. O(1) добавление/удаление, O(K) для K готовых fd.

**`kqueue`** (BSD/macOS) — аналог:
```cpp
int kq = kqueue();

struct kevent ev;
EV_SET(&ev, srv, EVFILT_READ, EV_ADD, 0, 0, nullptr);
kevent(kq, &ev, 1, nullptr, 0, nullptr);

struct kevent events[64];
int n = kevent(kq, nullptr, 0, events, 64, nullptr);
```

Семантически близко к epoll. Linux и BSD разошлись в API в 2002, и это разные подходы к одной задаче.

**Windows IOCP** (I/O Completion Ports) — ещё одна модель. Не «опрашивать готовность», а «получить завершённую операцию». Архитектурно ближе к async-IO.

Для **кросс-платформенности** обычно делают тонкую обёртку: `EventLoop::add(fd, callback)`, который под капотом использует epoll/kqueue/IOCP.

## Edge-triggered vs level-triggered

В **epoll** и **kqueue** два режима:

**Level-triggered (LT)** — поведение `poll`: «если есть данные — снова и снова сигналь». Если ваш код не успел всё прочитать, следующий `poll` снова покажет fd готовым.

**Edge-triggered (ET)** — сигналит **только** при изменении состояния: «было пусто, стало непусто». Если не прочитали всё за раз — `epoll_wait` не вернёт этот fd, пока не придёт ещё что-то.

ET **немного быстрее**, но требует **non-blocking IO** и **читать до EAGAIN**. Иначе пропустите данные.

В нашей версии — `poll`, который только LT. Простая модель.

## Реактор-цикл

```
main loop:
    готовые_fd = poll(все_fd, ...)
    
    для каждого готового fd:
        если listening fd:
            accept → новый client_fd
            добавить в список
        иначе если client fd:
            если можно читать: recv → накопить, парсить, broadcast
            если можно писать: send из per-client send_buffer
            если ошибка: close, удалить
```

Это **single-threaded event loop**. Никаких мьютексов на shared state — всё в одном потоке.

### Per-client send buffer

Когда мы делаем `broadcast`, нам нужно **записать** в N клиентов. Каждый `send` может вернуть `EAGAIN` (буфер ядра полный). Что делать?

В **thread-per-connection**: блокируем `send`, поток ждёт.

В **реакторе**: блокировать **нельзя** — это парализует весь сервер. Решение:
- Кладём данные в `send_buf` для каждого клиента.
- На следующей итерации, если `POLLOUT` готов, пишем оттуда.

```cpp
struct ClientState {
    int fd;
    std::string recv_buf;
    std::string send_buf;
};

void broadcast(...) {
    for (auto& kv : clients) {
        kv.second.send_buf += msg;
    }
}

// В реактор-цикле:
for (auto& pfd : pfds) {
    short events = POLLIN;
    if (!clients[pfd.fd].send_buf.empty()) {
        events |= POLLOUT;
    }
    pfd.events = events;
}

// При срабатывании POLLOUT:
ssize_t w = send(fd, s.send_buf.data(), s.send_buf.size(), 0);
if (w > 0) s.send_buf.erase(0, w);
```

Главное: **POLLOUT запрашиваем только когда есть что писать**. Иначе `poll` будет постоянно возвращать «готов писать» (level-triggered), занимая CPU.

## set_nonblocking

```cpp
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

Каждый сокет (включая listening!) переключаем в non-blocking. Тогда `accept`, `recv`, `send` возвращаются сразу, не блокируют.

Для `accept` это позволяет вызывать его в цикле, пока есть готовые соединения. Без non-blocking — после первого accept'а второй блокировал бы.

## Полная реализация — reactor_chat_server.cpp

(Пропускаем boilerplate socket/bind/listen — он как в главе 40.)

```cpp
std::unordered_map<int, ClientState> clients;
int next_id = 1;

while (true) {
    std::vector<pollfd> pfds;
    pfds.push_back({srv, POLLIN, 0});
    for (const auto& kv : clients) {
        short events = POLLIN;
        if (!kv.second.send_buf.empty()) events |= POLLOUT;
        pfds.push_back({kv.first, events, 0});
    }

    int n = ::poll(pfds.data(), pfds.size(), -1);
    if (n < 0) { if (errno == EINTR) continue; break; }

    // 1) listening socket
    if (pfds[0].revents & POLLIN) {
        while (true) {
            int c = ::accept(srv, nullptr, nullptr);
            if (c < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                break;
            }
            set_nonblocking(c);
            ClientState s; s.fd = c;
            s.name = "user_" + std::to_string(next_id++);
            clients.emplace(c, std::move(s));
            broadcast(clients, c, "* " + clients[c].name + " вошёл\n");
            clients[c].send_buf += "Привет, " + clients[c].name + "\n";
        }
    }

    // 2) клиенты
    for (size_t i = 1; i < pfds.size(); ++i) {
        int fd = pfds[i].fd;
        short re = pfds[i].revents;
        auto it = clients.find(fd);
        if (it == clients.end()) continue;
        ClientState& s = it->second;

        if (re & POLLIN) {
            char buf[1024];
            bool gone = false;
            while (true) {
                ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
                if (r == 0) { gone = true; break; }
                if (r < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    gone = true; break;
                }
                s.recv_buf.append(buf, r);
            }
            // парсим сообщения
            size_t pos;
            while ((pos = s.recv_buf.find('\n')) != std::string::npos) {
                std::string line = s.recv_buf.substr(0, pos);
                s.recv_buf.erase(0, pos + 1);
                if (line == "/quit") { gone = true; break; }
                broadcast(clients, fd, "<" + s.name + "> " + line + "\n");
            }
            if (gone) {
                std::string bye = "* " + s.name + " вышел\n";
                ::close(fd);
                clients.erase(it);
                broadcast(clients, -1, bye);
                continue;
            }
        }

        if ((re & POLLOUT) && !s.send_buf.empty()) {
            while (!s.send_buf.empty()) {
                ssize_t w = ::send(fd, s.send_buf.data(), s.send_buf.size(), MSG_NOSIGNAL);
                if (w < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    ::close(fd); clients.erase(it); break;
                }
                s.send_buf.erase(0, w);
            }
        }

        if (re & (POLLHUP | POLLERR | POLLNVAL)) {
            ::close(fd);
            clients.erase(it);
        }
    }
}
```

Ключевые моменты:

1. **`pfds` пересобирается каждую итерацию** — у некоторых клиентов появился send_buf, у других исчез.
2. **`accept` в цикле, пока EAGAIN** — на одну итерацию `poll` могло прийти несколько SYN.
3. **`recv` в цикле, пока EAGAIN** — то же, накапливаем всё, что пришло.
4. **`send` в цикле**, пока не отправили всё или EAGAIN.

## Тестирование

```bash
$ ./build/reactor_chat_server 9096 &
$ ./build/echo_client 127.0.0.1 9096   # Терминал 1
> hello from A
<- * user_2 вошёл
<- <user_2> from B

$ ./build/echo_client 127.0.0.1 9096   # Терминал 2
> from B
<- <user_1> from A
```

Работает. Один поток обслуживает обоих. Если запустить 100 клиентов — тоже один поток.

## Сравнение моделей

| Модель | Поток на клиента | Память | CPU | Сложность кода |
|--------|------------------|---------|-----|----------------|
| Thread-per-conn | 1 поток / N клиентов | 8 MB × N | context switch | простой |
| Reactor (poll) | 1 поток / 1000 клиентов | минимум | O(N) на opt | сложнее |
| Reactor (epoll/kqueue) | 1 поток / миллион | минимум | O(K) на opt | сложнее |
| Hybrid (reactor + worker pool) | 1 reactor + N workers | средне | масштабируется | сложнее всего |

В production:
- **Nginx**: один master + worker процессы, каждый — реактор.
- **Redis**: один реактор, всё в одном потоке.
- **Node.js**: один реактор (libuv).

Реактор — стандарт для C10K+ серверов.

## Cross-platform wrapper

Идея: интерфейс высокого уровня, реализация — epoll/kqueue/poll под капотом.

```cpp
class EventLoop {
public:
    void add(int fd, EventType type, Callback cb);
    void remove(int fd);
    void run();   // loop
};
```

Под Linux — `epoll_ctl/epoll_wait`. Под macOS — `kevent`. Под Windows — IOCP с другой моделью (асинхронность вместо опроса). Известные библиотеки:

- **libevent** — старая C-библиотека.
- **libuv** — Node.js core. Кросс-платформенная.
- **Boost.Asio** — C++ Boost library.
- **ASIO** (standalone).
- **liburing** (Linux io_uring — самый современный async API).

Используют **production** проекты, не пишут сами. Наш реактор — учебный.

## Async/await и coroutines

Реактор-код некомфортен: вместо линейного «прочитал → обработал» — fragmented callbacks. Это **callback hell**.

С C++20 пришли **coroutines**: пишем «async-await»-стиль, под капотом — реактор. Намного читабельнее.

```cpp
// Псевдо-C++20:
task<void> handle_client(int fd) {
    while (true) {
        std::string msg = co_await read_line(fd);
        if (msg == "/quit") break;
        co_await broadcast(msg);
    }
}
```

C++ coroutines сложны в реализации (низкоуровневые), но дают читабельный код. В нашей C++11-базе их нет. В Части VI поговорим обзорно.

## Главные правила главы

1. **Thread-per-connection не масштабируется.** Реактор для серверов.
2. **Non-blocking IO + полинг.** `O_NONBLOCK` через `fcntl`.
3. **`poll`** для маленьких/учебных, **`epoll`/`kqueue`** для production.
4. **Edge-triggered + non-blocking + читать до EAGAIN.** Не пропустите.
5. **Per-client send buffer.** Не блокируйте send в реакторе.
6. **`POLLOUT` запрашиваем только когда есть что писать.** Иначе CPU кипит.
7. **EAGAIN/EWOULDBLOCK — нормальная ситуация**, не ошибка.
8. **libuv/libevent/asio** для cross-platform. Не пишите сами в production.

## Маленькое упражнение

1. Запустите. Подключите несколько клиентов. Убедитесь, что один поток обслуживает всех.

2. Замерьте память сервера через `ps -o rss`. С 100 подключёнными клиентами — должен быть в десятки MB, не GB.

3. (Сложнее) Замените `poll` на `epoll` (если на Linux). Сравните скорость на тысяче клиентов.

4. (Сложнее) На macOS реализуйте через `kqueue`. Изучите `EV_SET`, `EVFILT_READ`, `EVFILT_WRITE`.

5. (Сложнее) Сделайте **hybrid** модель: главный реактор принимает соединения и передаёт их в **thread pool** worker'ов. Каждый worker — свой реактор. Это nginx-стиль.

6. (Очень сложно) Подключите libuv. Перепишите сервер на её API. Сравните читаемость.

7. Прочитайте оригинальный пост о C10K (Dan Kegel): http://www.kegel.com/c10k.html. Историческая ценность.

8. Прочитайте `man 2 epoll`, `man 2 kqueue`. Сравните API.

## Что дальше

Глава 44 — **свой бинарный протокол**. До сих пор клиенты слали строки, разделённые `\n`. Это нестабильно (что если в сообщении есть `\n`?). Сделаем фрейм-формат `[length:4][type:1][payload:N]`. Версионирование протокола, частичные чтения через кольцевой буфер.

Дальше — многокомнатный чат (45), шифрование+история (46), сборка и deployment (47). Финал Части V близок.
