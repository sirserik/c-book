# Глава 40. BSD-сокеты с нуля

Часть V — **TCP-чат**. Восемь глав сетевого программирования: сокеты, многопоточность, atomic, реактор-цикл, бинарные протоколы, многокомнатный чат. К концу — рабочий сервер и клиент, на котором можно общаться.

Эта глава — фундамент. **BSD-сокеты** — это POSIX API для сетевой работы. Тот же на Linux, macOS, в Cygwin/MinGW на Windows. Идея проста: ОС даёт «socket» — файловый дескриптор, который можно `read`/`write`, как обычный файл. Под капотом ОС обрабатывает TCP-пакеты, ACK'и, retransmission, окна. Программа видит **поток байтов**.

Напишем простой **echo-server**: принимает соединения, отвечает каждой строкой обратно. Плюс клиент. Это «hello world» сетевого программирования.

## Что такое socket

`socket` — это endpoint двусторонней коммуникации. Каждый сокет — это:
- **Адрес** (IP + порт): откуда/куда.
- **Тип**: TCP (потоковый, гарантированная доставка) или UDP (пакетный, без гарантий).
- **Файловый дескриптор**: программа работает с ним как с файлом — `read`, `write`, `close`.

Идея «всё файл» в Unix действительно глубокая. Сокет — это `fd`, как `stdin` или открытый файл. На нём работают `pread`/`pwrite`... ну, на сокетах их нет, но есть похожие `recv`/`send`.

## TCP vs UDP

**TCP** (Transmission Control Protocol):
- Поток байтов между двумя endpoints.
- Гарантированная доставка, в правильном порядке.
- Reconnect при разрывах.
- Управление потоком (sliding window).
- Прозрачно для приложения.
- Чуть медленнее UDP из-за overhead.

**UDP** (User Datagram Protocol):
- Дискретные пакеты (datagrams).
- Без гарантий — может потеряться, прийти из порядка, дублироваться.
- Минимальный overhead.
- Используется в DNS, видеоконференциях, играх.

Для **чата** TCP — естественный выбор. Гарантия доставки важна.

## API

POSIX-сокеты — функции из `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>`:

| Функция | Что делает |
|---------|-----------|
| `socket(domain, type, proto)` | Создать сокет, вернуть fd |
| `bind(fd, addr, len)` | Привязать сокет к адресу (для сервера) |
| `listen(fd, backlog)` | Перевести в режим прослушивания (для сервера) |
| `accept(fd, ...)` | Принять входящее соединение, вернуть новый fd |
| `connect(fd, addr, len)` | Подключиться к удалённому адресу (для клиента) |
| `send(fd, buf, n, flags)` | Послать данные |
| `recv(fd, buf, n, flags)` | Принять данные |
| `close(fd)` | Закрыть |
| `setsockopt(fd, ...)` | Настройки сокета |
| `getaddrinfo(host, port, ...)` | Резолв host:port в адреса |

`send`/`recv` похожи на `write`/`read` (которые тоже работают на сокетах). Разница — у них есть флаги (например, `MSG_PEEK` чтобы посмотреть без потребления).

## Жизненный цикл сервера

```
socket()
   ↓
setsockopt(SO_REUSEADDR)
   ↓
bind(addr:port)
   ↓
listen()
   ↓
while (true):
    accept() → новый fd для клиента
       ↓
    recv/send цикл
       ↓
    close(client)
   ↓
close(server)
```

Семь системных вызовов, и сервер готов.

## Жизненный цикл клиента

```
getaddrinfo(host, port)
   ↓
socket()
   ↓
connect(addr)
   ↓
send/recv цикл
   ↓
close()
```

Проще — клиент только подключается и общается, не слушает.

## socket()

```cpp
int fd = ::socket(AF_INET, SOCK_STREAM, 0);
```

- **`AF_INET`** — IPv4. Для IPv6 — `AF_INET6`. Для Unix sockets (локальные) — `AF_UNIX`.
- **`SOCK_STREAM`** — TCP. Для UDP — `SOCK_DGRAM`.
- **`0`** — protocol auto (для AF_INET + SOCK_STREAM это TCP).

Возвращает fd при успехе, -1 при ошибке (с `errno`).

## sockaddr_in для IPv4

Адрес в IPv4 описывается структурой:

```cpp
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET
    in_port_t      sin_port;     // порт в network byte order
    struct in_addr sin_addr;     // IP-адрес
    char           sin_zero[8];  // padding
};
```

Заполнить для прослушивания «всех интерфейсов на порту 9001»:

```cpp
sockaddr_in addr;
std::memset(&addr, 0, sizeof(addr));
addr.sin_family = AF_INET;
addr.sin_port = htons(9001);
addr.sin_addr.s_addr = htonl(INADDR_ANY);
```

`memset` нулями — обязательно. Поле `sin_zero` должно быть пустым.

**`htons` / `htonl`** (host-to-network short/long) — конверсия в **network byte order** (big-endian). Сетевые протоколы исторически big-endian. Без `htons` порт 9001 на Intel прочтётся неправильно.

`INADDR_ANY` (0.0.0.0) — «любой интерфейс на этой машине». Альтернатива — `INADDR_LOOPBACK` (127.0.0.1) — только localhost.

### sockaddr и его потомки

В API часто параметр — `sockaddr*`, не `sockaddr_in*`. Это «базовый» тип. Реально для IPv4 передаём `sockaddr_in*`, приведя через cast:

```cpp
bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

Это как полиморфизм в C: одна функция, разные структуры. Внутри `bind` смотрит на `addr.sin_family` и понимает, что это.

## bind()

```cpp
::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

Привязать сокет к адресу. После этого `accept` будет принимать соединения именно на этот IP:port.

Самая частая ошибка — **`EADDRINUSE`** «адрес занят». Бывает, если сервер недавно закрылся, а ОС держит порт в `TIME_WAIT` (защита от перепутанных пакетов от предыдущего соединения).

**SO_REUSEADDR**:

```cpp
int yes = 1;
::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
```

Сказать ядру «разреши перепривязку даже в TIME_WAIT». Стандартный приём для серверов, которые часто перезапускаются (разработка, тесты).

## listen()

```cpp
::listen(srv, 16);
```

Перевести сокет в **режим прослушивания**. С этого момента ОС начинает принимать TCP handshake (SYN-SYN/ACK-ACK) от клиентов и **складывать готовые соединения** в очередь.

`16` — **backlog**, длина очереди. Если приходит больше клиентов, чем мы успеваем обработать через `accept` — лишние отбрасываются (получают TCP reset). На простом сервере 16 хватает; на нагруженных — 128, 1024, 65535.

## accept()

```cpp
sockaddr_in cli;
socklen_t cli_len = sizeof(cli);
int c = ::accept(srv, reinterpret_cast<sockaddr*>(&cli), &cli_len);
```

**Блокирующий** вызов. Ждёт, пока в очереди появится готовое соединение, потом возвращает **новый fd** для общения с этим клиентом.

Параметры `cli` / `cli_len` — выходные: ОС заполняет адрес клиента (его IP/port). `cli_len` — сначала указывает размер буфера, после вызова — сколько реально записано.

После `accept`:
- **`srv`** остаётся в listen-режиме для следующих клиентов.
- **`c`** — новый fd, через него `send`/`recv`.

Один сервер, **один accept-loop**, много client-fd одновременно (если многопоточность; пока один за раз).

## recv() / send()

```cpp
char buf[1024];
ssize_t n = ::recv(c, buf, sizeof(buf), 0);
```

Прочитать до `n` байтов от клиента. Возвращает:
- **0** — клиент **закрыл** соединение (graceful close). Это нормальный сценарий!
- **> 0** — число прочитанных байт (может быть меньше запрошенного — short read).
- **-1** — ошибка (`errno` уточняет).

Особый `errno`:
- **`EINTR`** — прерывание сигналом. Повторяем (как мы делали в shell-утилитах).
- **`ECONNRESET`** — клиент сбросил без graceful close (kill -9 на клиенте). Тоже «нормально» — клиент пропал.

```cpp
ssize_t sent = ::send(c, buf, n, 0);
```

Отправить. Возвращает реально отправленное (может быть меньше). Нужен цикл, как для `write` в shell:

```cpp
ssize_t total = 0;
while (total < n) {
    ssize_t s = ::send(c, buf + total, n - total, 0);
    if (s < 0) {
        if (errno == EINTR) continue;
        break;
    }
    total += s;
}
```

Флаги (последний параметр) обычно `0`. Реже используются:
- `MSG_PEEK` — посмотреть, не потребляя.
- `MSG_DONTWAIT` — non-blocking даже на blocking-сокете.
- `MSG_NOSIGNAL` — на Linux: не посылать SIGPIPE при записи в закрытый сокет. Очень полезно для серверов.

## close()

```cpp
::close(c);
```

Закрыть. На TCP-сокете это инициирует **graceful close** (FIN-пакет). Другая сторона получит `recv() == 0`.

Альтернатива — **`shutdown(fd, SHUT_WR)`** — закрыть **только запись**, оставив возможность читать ответ. Полезно для протоколов вроде HTTP, где клиент шлёт запрос, говорит «я кончил», и слушает ответ.

## getaddrinfo для клиента

```cpp
addrinfo hints;
std::memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_UNSPEC;     // и IPv4, и IPv6
hints.ai_socktype = SOCK_STREAM;

addrinfo* res = nullptr;
::getaddrinfo("example.com", "9001", &hints, &res);
```

`getaddrinfo` делает DNS-резолв: имя → список IP-адресов. Возвращает связный список `addrinfo` структур.

`AF_UNSPEC` — «не важно, IPv4 или IPv6». ОС вернёт обе версии, мы попробуем поочерёдно.

Перебираем:

```cpp
int fd = -1;
for (addrinfo* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
}
::freeaddrinfo(res);
```

`::freeaddrinfo(res)` обязательно — это malloc'нутый связный список.

Это **правильный** способ работы с адресами. **Не** парсить «1.2.3.4:5678» руками — `getaddrinfo` умеет и IPv4-литералы, и IPv6, и DNS-имена.

## inet_ntop для печати

После `accept` мы получили `sockaddr_in` клиента. Как напечатать «1.2.3.4»?

```cpp
char ip[INET_ADDRSTRLEN] = {0};
::inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
std::cout << ip << ":" << ntohs(cli.sin_port);
```

`inet_ntop` (network-to-presentation) превращает бинарный адрес в текстовый. `INET_ADDRSTRLEN` (16) — достаточный буфер для «255.255.255.255\0».

Для IPv6 — `AF_INET6` и `INET6_ADDRSTRLEN` (46).

## Echo-server — полная сборка

`utils/echo_server.cpp`:

```cpp
int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::atoi(argv[1]) : 9001;

    int srv = ::socket(AF_INET, SOCK_STREAM, 0);

    int yes = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(srv, 16);

    std::cout << "слушаем на :" << port << "\n";

    while (true) {
        sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int c = ::accept(srv, reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (c < 0) { if (errno == EINTR) continue; break; }

        // recv/send loop для одного клиента
        char buf[1024];
        while (true) {
            ssize_t n = ::recv(c, buf, sizeof(buf), 0);
            if (n == 0) break;
            if (n < 0) { if (errno == EINTR) continue; break; }
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t s = ::send(c, buf + sent, n - sent, 0);
                if (s < 0) { if (errno == EINTR) continue; break; }
                sent += s;
            }
        }
        ::close(c);
    }
    ::close(srv);
}
```

100 строк. Это **полнофункциональный** TCP-сервер. Работает с `telnet`, `nc`, `curl`, любым клиентом, говорящим TCP.

## Client

`utils/echo_client.cpp`:

```cpp
int main(int argc, char* argv[]) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char* port = (argc > 2) ? argv[2] : "9001";

    addrinfo hints; std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    ::getaddrinfo(host, port, &hints, &res);

    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) break;
        line += '\n';
        ::send(fd, line.data(), line.size(), 0);

        char buf[1024];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        std::cout << "<- " << std::string(buf, n);
    }
    ::close(fd);
}
```

Пользователь вводит строку → клиент шлёт → ждёт ответ → печатает.

## Запуск

Терминал 1:
```bash
$ ./build/echo_server 9099
слушаем на :9099
```

Терминал 2:
```bash
$ ./build/echo_client 127.0.0.1 9099
Подключён к 127.0.0.1:9099.
> hello
<- hello
> world
<- world
```

В терминале 1 параллельно:
```
Клиент 127.0.0.1:54032 подключён
клиент отключился
```

Можно подключиться и через `telnet localhost 9099` — будет работать.

## Что наш сервер НЕ умеет

Прежде чем переходить к улучшениям — что сейчас не работает:

1. **Один клиент за раз.** Пока обслуживаем одного — другие висят в backlog (или отказываются после переполнения).
2. **Любой сигнал прерывает.** Ctrl+C на сервере убивает.
3. **Нет защиты от медленных клиентов.** Если клиент висит на recv (медленная сеть) — сервер ждёт.
4. **Никакой логики чата.** Только echo.

Решения:

- **Один-поток-на-клиента**: в `accept`-loop форкаем поток для каждого клиента. Это глава 41.
- **Реактор**: один поток обрабатывает много клиентов через `select`/`poll`/`epoll`. Глава 43.
- **Свой протокол** для разделения сообщений. Глава 44.

## SO_LINGER и другие настройки

Несколько других полезных setsockopt:

- **SO_LINGER** — что делать при close с непрочитанными данными. По умолчанию ОС асинхронно сбрасывает их. С `linger {true, N}` — close ждёт N секунд.
- **TCP_NODELAY** — отключить Nagle's algorithm. Полезно для интерактивных приложений (chat, gaming): каждый send отправляется немедленно, не накапливается.
- **SO_KEEPALIVE** — periodic keepalive-пакеты. Обнаруживают «мёртвые» соединения.
- **SO_RCVBUF / SO_SNDBUF** — размер буферов в ядре. Большие — для bulk-передачи; маленькие — для low-latency.

Эти настройки — на 90% можно не трогать. Дефолты ОС разумные.

## TIME_WAIT и почему важен SO_REUSEADDR

Когда TCP-соединение закрывается через `close`, оно проходит несколько состояний. Последнее у инициатора close — **TIME_WAIT** на 2×MSL (Maximum Segment Lifetime, обычно 30-120 секунд).

Зачем: страховка от ложных пакетов. Если предыдущее соединение оставило пакет «в эфире», и новое соединение использовало бы тот же ip:port — пакет может прийти в новое соединение и испортить данные.

Для **разработчика** это значит: после `Ctrl+C` сервера попытка перезапустить = `EADDRINUSE`. Подождать минуту или поставить `SO_REUSEADDR`.

В production обычно настраивают и оставляют `SO_REUSEADDR=1`.

## Главные правила главы

1. **Socket = fd**. Все правила POSIX file IO применимы.
2. **TCP для гарантированной доставки**, UDP для скорости.
3. **htons/htonl** для портов и IP. Network byte order — big-endian.
4. **SO_REUSEADDR** на серверах — обязательно для удобного перезапуска.
5. **`recv() == 0`** означает graceful close. `< 0` — ошибка.
6. **getaddrinfo** для DNS-резолва и IPv4/IPv6 совместимости.
7. **`send`/`recv` могут возвращать меньше запрошенного.** Цикл обязателен.
8. **MSG_NOSIGNAL** для серверов — без SIGPIPE.

## Маленькое упражнение

1. Соберите. Запустите сервер. Подключитесь клиентом. Поиграйтесь.

2. Запустите параллельно два клиента (два терминала). Что произойдёт? Один работает, второй висит до отключения первого.

3. Запустите `telnet localhost 9099` вместо нашего клиента. Поработает?

4. Попробуйте `curl -X POST -d "hello" http://localhost:9099` — наш сервер не понимает HTTP, но что-то увидите. Что?

5. Сделайте сервер на IPv6: измените `AF_INET` на `AF_INET6`, `sockaddr_in` на `sockaddr_in6`. Проверьте через `telnet ::1 9099`.

6. (Сложнее) Добавьте обработку SIGINT в сервере: закрыть `srv` сокет, выйти из accept-loop корректно.

7. (Сложнее) Сделайте «лимит на сообщение»: после получения N байтов от клиента — отключайте его.

8. (Сложнее) Прочитайте `man 2 socket`, `man 2 bind`, `man 7 tcp`. Особое внимание — `tcp(7)` со списком SO_* опций.

## Что дальше

Глава 41 — **многопоточность**: `std::thread`, `std::mutex`, `std::condition_variable`. Делаем модель «один поток на клиента» — сервер сможет обслуживать многих параллельно. Обсуждаем deadlock, race conditions, как защищать общие данные.

Дальше — atomic и memory ordering (42), реактор `select`/`poll`/`epoll`/`kqueue` (43), бинарный протокол (44), многокомнатный чат (45), шифрование+история (46), сборка и деплой (47).
