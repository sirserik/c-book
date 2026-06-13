# Глава 47. Сборка и деплой

Финальная глава Части V. У нас в `demo-chat/` — 1300+ строк работающего сетевого кода. Сервер запускается локально через `./build/rooms_chat_server`. Но **production** — это другое. В этой главе:

1. Static vs dynamic linking — какой нужен для портабельности.
2. glibc vs musl — почему статический glibc проблемен.
3. Docker для C++ — минимальный образ.
4. Итоги Части V.
5. Что не реализовали.

## Static vs dynamic — снова

Из главы 7:
- **Dynamic linking**: бинарь маленький, требует библиотеки в системе при запуске.
- **Static linking**: все библиотеки внутри бинаря, размер большой, ничего не нужно снаружи.

Для **production-сервера** часто нужен static:
- Кладёшь файл на чужую машину — работает без установки зависимостей.
- Минимальные Docker-образы (без libc на runtime stage).
- Меньше surprise'ов «у меня libstdc++ 9, у тебя 11, не совместимо».

Минусы:
- Бинарь больше (5-10 MB вместо 100 KB).
- Не обновляется безопасностью (новая уязвимость в OpenSSL — пересобирать всё, что её используют).

## g++ -static — что не так с glibc

Пробуем:

```bash
$ g++ -static -std=c++11 main.cpp -o server
```

На большинстве Linux это даёт **warning** или **fail** на `getaddrinfo` / `gethostbyname` / похожие: «error: function not implemented»:

```
warning: Using 'getaddrinfo' in statically linked applications requires
at runtime the shared libraries from the glibc version used for linking
```

Причина: **glibc** для некоторых функций (включая DNS-resolver) использует **NSS** (Name Service Switch). NSS подгружает плагины **динамически в рантайме**. Статически слинковать DNS-resolver технически невозможно — он должен dlopen свои плагины.

Эта неприятность мешает «настоящему» static-binary на glibc.

## musl — альтернатива

**musl libc** — альтернативная C-стандартная библиотека, написанная с нуля для:
- Простоты (компактный код, меньше surprise).
- Корректной static linking (без NSS-issues).
- Скорости и компактности (меньше memory footprint).

Дистрибутивы на musl: **Alpine Linux**, **Void Linux musl edition**.

С musl static linking **работает чисто**:

```bash
# На Alpine:
$ apk add build-base
$ g++ -static -static-libstdc++ -static-libgcc -std=c++11 -O2 main.cpp -o server
$ file server
server: ELF 64-bit LSB executable, statically linked, ...
$ ./server   # запускается на любой Linux машине
```

Размер: ~3-5 MB для нашего chat-server. Без зависимостей.

Чтобы делать musl-сборку с macOS — кросс-компилятор сложно. Проще через Docker.

## Docker для C++

Docker — стандартный способ packaging серверов. Идея: «контейнер» = весь окружение программы (бинарь + библиотеки + конфиг).

### Многоступенчатый build

Для C++ идиоматичен **multi-stage build**:

```dockerfile
# Stage 1: builder
FROM alpine:3.19 AS builder
RUN apk add --no-cache build-base make
WORKDIR /src
COPY . .
RUN make MODE=release \
    CXXFLAGS="-std=c++11 -O2 -DNDEBUG -static -static-libstdc++ \
              -static-libgcc -pthread -Iinclude -MMD -MP" \
    LDFLAGS="-static -pthread"

# Stage 2: minimal runtime
FROM scratch
COPY --from=builder /src/build/rooms_chat_server /chat-server
EXPOSE 9001
ENTRYPOINT ["/chat-server", "9001"]
```

Что происходит:

**Stage 1** — Alpine с инструментами сборки. Это «жирный» образ (~500 MB с компилятором). Но мы используем его только для сборки.

**Stage 2** — **`scratch`**, пустой образ. Туда копируется **только** наш бинарь. Никакой libc, никакого shell, никаких инструментов.

Итог: образ **~5 MB**, содержит **только** наш chat-server. Меньше surface для атак, быстрее загрузка.

### Сборка и запуск

```bash
$ docker build -t my-chat .
[+] Building 30s
...
Successfully built abc123

$ docker images
my-chat    latest    abc123    5.2MB

$ docker run -p 9001:9001 my-chat
Rooms chat-server слушает на :9001
```

`-p 9001:9001` пробрасывает порт хоста в контейнер.

С хоста (или другого контейнера) можем подключаться к `localhost:9001`. Внутри контейнера — `0.0.0.0:9001`.

### Базовые образы для C++

Если **scratch** не подходит (нужен shell для дебага, или образ запускается с пользователя non-root):

- **`alpine:3`** (~5 MB) — минимальный Linux с busybox. musl libc.
- **`distroless/cc`** (Google, ~25 MB) — Debian-минимальный, нет shell, есть libc/libstdc++.
- **`debian:slim`** (~70 MB) — Debian без extras.
- **`ubuntu`** (~70 MB) — Ubuntu.

Для серверов выбирают **alpine** или **distroless** — баланс размера и compatibility.

## Cross-compilation

С macOS собирать Linux-бинарь? Несколько подходов:

1. **Docker для сборки** — самый простой. Внутри alpine получаем настоящую сборку.

2. **Cross-compiler**: `aarch64-linux-musl-gcc`, `x86_64-linux-musl-gcc`. Установить через `brew install musl-cross`. Сложнее.

3. **VM на Linux** — старый школьный путь.

4. **CI/CD**: GitHub Actions, CircleCI собирает в нужном окружении.

Для production — обычно CI/CD строит и пушит в registry (Docker Hub, GHCR, GCR). Скачиваешь готовый образ на сервер, запускаешь.

## Что нужно для production-деплоя

Полный список того, что у нас **нет**, но в реальном проекте надо:

### Logging

Сейчас наш сервер `std::cout << ...`. В production:
- **Структурированные логи**: JSON-форматные. Поисковые системы (Elastic, Splunk) парсят.
- **Уровни**: DEBUG/INFO/WARN/ERROR.
- **Ротация**: лог-файлы не должны расти бесконечно.
- **Tracing**: distributed tracing для микросервисов (Jaeger, Zipkin).

Библиотеки: **spdlog**, **glog**, **fmt**.

### Метрики

Что-то надо мониторить:
- Активных клиентов.
- Latency сообщений.
- Memory usage.
- Error rate.

Стандарт — **Prometheus** + **Grafana**. Программа экспортирует метрики по HTTP, Prometheus собирает.

C++ библиотеки: **prometheus-cpp**.

### Конфигурация

Жёсткое кодирование `port = 9001` — плохо. Должно быть:
- Через **env vars** (Docker-style): `PORT=9001`.
- Через **CLI args**: `--port 9001`.
- Через **config файл** (YAML/TOML/JSON).

Библиотеки: **CLI11** для argv-парсинга, **toml++** для TOML.

### Health checks

`/healthz` endpoint, который возвращает 200 OK если сервер живой. Kubernetes/Docker используют для liveness/readiness probes.

### Graceful shutdown

При SIGTERM:
1. Перестать принимать новые соединения.
2. Отправить «server shutting down» текущим.
3. Подождать, пока они отключатся (или timeout).
4. Закрыть всё, fsync history.
5. Выйти.

Без этого — резкое прерывание соединений, потеря данных.

### Authentication / authorization

Сейчас «представляюсь, имя — Alice». Никакой проверки. В production:
- **Аутентификация**: пароль, OAuth, JWT-токены.
- **Avalanche-protection**: rate-limiting на login.
- **Ban-list**: блок недобросовестных юзеров.

### Database вместо файлов

Наш history — bin-файл. На миллионах сообщений медленно. Production — PostgreSQL/Redis/Cassandra. Тогда:
- Индексы по комнате/времени.
- Search.
- Backup/replication.

### Horizontal scaling

Один сервер не выдержит 1M+ клиентов. Решения:
- **Multiple instances** + **load balancer** (HAProxy, nginx, AWS ALB).
- **Sticky sessions** — клиент возвращается к тому же серверу.
- **Inter-server communication** — Redis pub/sub для broadcast между instances.

Этой инфраструктуры может быть **больше**, чем кода самого сервера.

## Часть V — итог

`demo-chat/` финальный объём: **~1300 строк** C++ кода + ~1300 строк глав.

Что построили:
- **Echo server/client** (40) — основа socket API.
- **Threaded chat** (41) — multi-client, mutex, condition_variable, deadlock.
- **Atomic demo** (42) — race condition, memory ordering, lock-free.
- **Reactor chat** (43) — poll-based, single-thread, scale-friendly.
- **Binary protocol** (44) — framing, version, partial reads.
- **Rooms chat** (45) — multi-room with rooms map, FSM на клиенте.
- **XOR + History** (46) — учебное шифрование, append-only лог.
- **Docker** (этот) — packaging для production.

Освоено:
- **POSIX sockets**: `socket`/`bind`/`listen`/`accept`/`connect`/`send`/`recv`/`getaddrinfo`.
- **`htons`/`htonl`/`inet_ntop`** для адресов.
- **`SO_REUSEADDR`**, **`MSG_NOSIGNAL`**, **`O_NONBLOCK`**.
- **`std::thread`/`mutex`/`condition_variable`/`atomic`** — concurrency.
- **`poll`/`select`/`epoll`/`kqueue`** — event-driven IO.
- **Length-prefixed framing** + аккумулятор для частичных reads.
- **Реактор-pattern** vs thread-per-connection.
- **Forward secrecy, TLS** — обзорно.
- **Append-only persistence**.
- **Docker multi-stage build**.

## Что не реализовано

Большой список production-фич:
- TLS/encryption между клиентами и сервером.
- Authentication.
- Configuration (env/CLI/file).
- Structured logging.
- Prometheus metrics.
- Graceful shutdown на SIGTERM.
- Database вместо файлов для истории.
- Multi-server clustering.
- E2E encryption.
- Client SDK на нескольких языках.
- Mobile push notifications.
- Voice/video через WebRTC.
- Reactions, threads, edit history.
- Spam protection, content moderation.

Каждое — недели разработки. Production-чат типа Slack/Discord — это **сотни инженеров на годы**. У нас — концептуальный фундамент.

## Что выучили концептуально

После Части V у вас понимание:

- **Как устроено сетевое программирование** на Unix.
- **Как масштабируются серверы** до сотен тысяч соединений.
- **Какие модели concurrency** существуют (threads, reactor, async/await).
- **Что такое framing-протоколы** и зачем они нужны.
- **Зачем TLS и Diffie-Hellman**.
- **Как упаковать сервер** в Docker.

Это базовый набор **системного программиста**. Применим к серверам игр, чатам, HTTP-серверам, message queues — везде, где сеть.

## Часть V закрыта

Большая Часть V (главы 40-47, 8 глав) — самая «системная» в книге. Если все четыре проекта (RPG → shell → DB → chat) у вас собираются и работают — вы освоили C++ на уровне хорошего senior-разработчика.

## Главные правила всей Части V

1. **Sockets — это fd**. POSIX file IO применяется.
2. **TCP — поток, не пакеты.** Framing обязателен.
3. **Один поток на клиента не масштабируется** — реактор для серьёзных нагрузок.
4. **atomic для простого, mutex для сложного, реактор лучше обоих** для серверов.
5. **MSG_NOSIGNAL** на серверах — против SIGPIPE.
6. **Length-prefixed framing** — стандарт для бинарных протоколов.
7. **«Don't roll your own crypto»** — используйте TLS и проверенные библиотеки.
8. **Docker scratch + static binary** — минимальные production-образы.

## Маленькое упражнение

1. Соберите Docker-образ: `docker build -t my-chat .`. Запустите: `docker run -p 9001:9001 my-chat`.

2. Замерьте размер образа: `docker images my-chat`. Должно быть < 10 MB.

3. (Сложнее) Замените `FROM scratch` на `FROM alpine` — что изменится в размере?

4. (Сложнее) Подключите `spdlog` для логирования вместо std::cout. Добавьте уровни и форматирование.

5. (Сложнее) Подключите **prometheus-cpp** для экспорта метрик. Покажите счётчик «активных клиентов».

6. (Сложнее) Реализуйте graceful shutdown: SIGTERM → закрыть accept-socket → дождаться отключения активных клиентов → выход.

7. (Очень сложно) Подключите OpenSSL для TLS. Сертификат через `mkcert` для разработки.

8. (Очень сложно) Разверните в production: купите VPS (DigitalOcean/Hetzner), настройте systemd unit, поднимите nginx как reverse proxy, подключите Let's Encrypt.

## Что дальше

**Часть V закрыта.**

**Часть VI** (главы 48-51) — финальный блок, **C++17 как бонус**. До этого вся книга на C++11. В этой части — обзор того, что появилось в C++14/17 и почему это **меньше boilerplate**:

- **48**: `optional`, `variant`, `any`, `if constexpr`, structured bindings.
- **49**: `std::filesystem`.
- **50**: `std::string_view`, parallel algorithms, `std::variant` для tagged unions.
- **51**: Куда дальше — обзор C++20/23 (concepts, ranges, coroutines, modules) + маршрут роста.

После 51-й главы — финал книги. Осталось 4 главы.
