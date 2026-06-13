# Глава 45. Многокомнатный чат

Реактор у нас есть (43), бинарный протокол есть (44). Соединим — добавим **комнаты**. Пользователь шлёт `Hello` с ником, `Join` с именем комнаты, потом `Message`'и. Сообщения видны **только участникам той же комнаты**.

Это уже похоже на настоящий Slack/Discord/IRC — на маленьком масштабе.

К концу главы у нас `rooms_chat_server` поверх готовой инфраструктуры. ~200 строк кода — благодаря тому, что протокол и реактор уже сделаны.

## Что добавляется

Над реактор-сервером из главы 43:
- **Каждый клиент имеет** `name` (после Hello) и `room` (после Join).
- **Структура комнат**: `unordered_map<string, set<int>>` — имя комнаты → множество fd.
- **Команды по типам сообщения**:
  - `Hello` → установить name.
  - `Join` → войти в комнату (выйти из предыдущей).
  - `Leave` → выйти.
  - `Message` → broadcast в текущую комнату.
- **Notify** от сервера: «вы вошли», «X присоединился», и т.п.

## Структуры данных

```cpp
struct ClientState {
    int fd;
    std::string name;       // пусто, пока не Hello
    std::string room;       // пусто = не в комнате
    std::vector<uint8_t> recv_buf;
    std::vector<uint8_t> send_buf;
};

std::unordered_map<int, ClientState> clients;
std::unordered_map<std::string, std::unordered_set<int>> rooms;
```

Двойная индексация:
- `clients[fd]` — что за клиент с этим fd.
- `rooms[name]` — кто в комнате `name`.

При входе в комнату вставляем fd в `rooms[room]`. При выходе — удаляем.

`unordered_set<int>` для участников — O(1) вставка/удаление, без дубликатов.

## Команды-обработчики

`process_message` — диспетчер по типу:

```cpp
void process_message(...) {
    switch (type) {
        case chat::kHello:
            c.name = payload;
            enqueue_frame(c, kNotify, "Привет, " + c.name);
            break;
        case chat::kJoin:
            if (c.name.empty()) {
                enqueue_frame(c, kNotify, "сначала Hello");
                break;
            }
            leave_room(c, rooms);   // если был в другой
            c.room = payload;
            rooms[c.room].insert(fd);
            broadcast_room(rooms, c.room, fd, kNotify,
                          "* " + c.name + " вошёл");
            break;
        case chat::kLeave:
            leave_room(c, rooms);
            break;
        case chat::kMessage:
            if (c.room.empty()) break;
            broadcast_room(rooms, c.room, fd, kMessage,
                          "<" + c.name + "> " + payload);
            break;
    }
}
```

**Состояние** клиента — это маленький FSM:
1. **Just connected** — name пусто. Можно только Hello.
2. **Hello'd** — name есть, room пуст. Можно Join.
3. **In room** — name + room. Можно Message, Leave, Join (в другую).

Игнорировать «недопустимые в текущем состоянии» сообщения — корректное поведение.

## leave_room — сложнее, чем кажется

```cpp
void leave_room(...) {
    if (c.room.empty()) return;
    std::string room = c.room;
    rooms[room].erase(fd);
    if (rooms[room].empty()) rooms.erase(room);
    broadcast_room(rooms, room, fd, kNotify,
                  "* " + c.name + " вышел из " + room);
    c.room.clear();
}
```

Тонкости:
1. **Удалить fd из rooms[room]** — раньше очистки `c.room`.
2. **Если комната пустая** — удалить её из map. Иначе пустые комнаты копятся.
3. **Broadcast** — оповестить оставшихся. Делать **после** удаления, иначе оставивший получит «вышел» сам.
4. **Очистить `c.room`** — клиент больше не в этой комнате.

При **disconnect** клиента — тоже надо `leave_room` сначала. У нас в реактор-цикле перед `close(fd)`:

```cpp
if (drop) {
    leave_room(clients, rooms, fd);
    ::close(fd);
    clients.erase(it);
}
```

Иначе rooms[room] будет содержать stale fd — broadcast попытается отправить на закрытый дескриптор. С `MSG_NOSIGNAL` это безопасно (вернёт `EPIPE`), но логически мусор.

## broadcast_room

```cpp
void broadcast_room(..., const std::string& room, int from_fd,
                    uint8_t type, const std::string& payload) {
    auto it = rooms.find(room);
    if (it == rooms.end()) return;
    for (int fd : it->second) {
        if (fd == from_fd) continue;
        auto ci = clients.find(fd);
        if (ci == clients.end()) continue;
        enqueue_frame(ci->second, type, payload);
    }
}
```

Идём по `rooms[room]`, для каждого fd кладём фрейм в send_buf. Реактор-цикл потом сам отправит, когда POLLOUT станет готов.

**Не блокируем send** прямо здесь — мы в одном потоке, send мог бы заблокировать весь сервер.

## Mutex vs shared_mutex

В **многопоточном** чате (как глава 41) надо защитить `rooms` мьютексом. Но что если **читателей** много (broadcast в большую комнату), а **писателей** мало (join/leave редко)?

C++17 даёт **`std::shared_mutex`**:

```cpp
std::shared_mutex rooms_mtx;

void broadcast(...) {
    std::shared_lock<std::shared_mutex> lock(rooms_mtx);  // ← shared
    // ... несколько потоков могут читать одновременно ...
}

void join_room(...) {
    std::unique_lock<std::shared_mutex> lock(rooms_mtx);   // ← exclusive
    // ... только один писатель ...
}
```

`shared_lock` — много читателей одновременно. `unique_lock` — эксклюзивный доступ для записи.

Если читателей значительно больше — `shared_mutex` снимает bottleneck.

В **нашем реакторе** — однопоточно, никаких мьютексов не нужно. Преимущество реактор-подхода.

## Multi-room vs one-room-per-client

В нашем варианте клиент **в одной комнате**. Это упрощение.

Реальные чаты (IRC, Discord, Slack):
- Клиент может быть **во многих комнатах одновременно**.
- Сообщение в одной комнате — broadcast только этой.
- Прерванное соединение — клиент остаётся в комнатах для истории (offline messages).

Для multi-room:

```cpp
struct ClientState {
    std::set<std::string> rooms;
};
```

При message нужно специфицировать **target room**:

```cpp
struct MessagePayload {
    std::string room;
    std::string text;
};
```

Это уже не строка, а struct. Сериализация — через свой формат внутри payload или через protobuf.

В IRC решено иначе: команда `PRIVMSG #channel :hello` — target в самой команде. У нас оставим simple version.

## Слабые места реализации

### Race на disconnect и broadcast

Сценарий:
1. Поток A читает recv в клиенте X → `Message`.
2. broadcast_room итерирует rooms[r], отправляет всем кроме X.
3. fd Y параллельно отключается — `close(Y)`.
4. broadcast_room шлёт на Y — `EPIPE`.

С `MSG_NOSIGNAL` — безопасно (просто ошибка). Без — SIGPIPE убивает сервер.

В реакторе (однопоточно) этой гонки нет — broadcast и close в одном потоке.

### Очередь send_buf переполняется

Если клиент очень медленный (или вообще завис), `send_buf` накапливается. На частом broadcast — гигабайты в памяти на одного клиента.

Решение:
- **Лимит на размер send_buf** (например, 1 MB). Если превышен — disconnect.
- **Drop policy**: при переполнении выбрасывать старые сообщения, не новые.

В нашем коде — без лимита. Учебное упрощение.

### Per-room mutex (если бы было multi-threading)

Если бы делали multi-threaded с разными мьютексами на каждую комнату — могли бы получить deadlock при cross-room операциях. Например, /move — клиент из A в B одновременно с другим из B в A.

Решение — **per-room mutex с отсортированным порядком взятия** (взять lock'и в алфавитном порядке имён комнат).

В реакторе — все в одном потоке, нет проблемы.

## Полная реализация

`rooms_chat_server.cpp` объединяет:
- Реактор-цикл (как в главе 43).
- Framing-протокол через `chat::try_decode` (глава 44).
- Управление комнатами.

```cpp
while (true) {
    poll(...);
    
    // accept
    // for each client:
    //   recv → acc → try_decode loop
    //     process_message → enqueue Notify/Message в send_buf
    //   send out send_buf when POLLOUT
}
```

Всё в одном потоке. Без мьютексов. Простой код.

Сборка:
```bash
$ make
$ ./build/rooms_chat_server 9001
Rooms chat-server слушает на :9001
```

Чтобы протестировать, нужен **framing-aware** клиент. У нашего `echo_client` нет поддержки протокола — это упражнение читателя в конце главы.

## Что в production-чатах

Slack, Discord, IRC — **миллионы** пользователей. Что выходит за рамки нашей версии:

### Persistence

История сообщений в БД. Чтобы можно было прочитать офлайн, scroll-up, поиск. Глава 46 затронет это для нашей мини-версии.

### Federation / distributed

Сервер не один — кластер. IRC — старые federation servers. Matrix — современный протокол с decentralized серверами. Discord — закрытый кластер.

### Identity

Учётные записи, OAuth, регистрация. Не просто «Hello, моё имя X», а **аутентификация**.

### Voice/video

WebRTC, peer-to-peer audio. Совсем другая инфраструктура.

### End-to-end encryption

Сервер **не видит** содержимое сообщений (Signal). Сложная криптография: Diffie-Hellman, Double Ratchet, X3DH. Очень trustworthy для приватности.

В главе 46 коснёмся basic шифрования (XOR pre-key) — учебный пример.

### Anti-spam, anti-abuse

Rate limiting, captcha, ban-list. Серверная инфраструктура отдельная.

### Push notifications

Когда клиент офлайн, push на телефон. APNs/FCM.

### Search

Elastic/Algolia для поиска по истории.

Все эти системы — отдельные сервисы вокруг chat-server. Сам chat-server в production — обычно довольно простой кусок.

## Главные правила главы

1. **Двойная индексация** `clients[fd]` + `rooms[name]` — для эффективных операций.
2. **Удаление пустых комнат** — иначе утечка.
3. **leave_room ДО close(fd)** при disconnect.
4. **broadcast не блокирует** — складывает в send_buf, реактор отправит.
5. **shared_mutex для read-heavy** структур, если многопоточно.
6. **Простое FSM на клиенте** — initial / hello'd / in-room.
7. **Лимит на send_buf** — против медленных клиентов.
8. **Реактор > thread-per-conn** для chat — общая память без мьютексов.

## Маленькое упражнение

1. Соберите `rooms_chat_server`. Запустите.

2. Реализуйте **framing-aware клиент**: `chat_client.cpp` который шлёт Hello/Join/Message фреймы. Подсказка — переиспользуйте `chat::encode` и `chat::try_decode`.

3. Подключите двух клиентов через свой chat_client. Один Join "general", второй тоже. Обменяйтесь сообщениями.

4. Третий клиент Join "secret". Сообщения от него видны только участникам "secret".

5. (Сложнее) Поддержите **multi-room** для одного клиента. Команда `/msg <room> <text>`.

6. (Сложнее) Команда `/who [room]` — список участников. Отправлять как Notify с форматированным текстом.

7. (Сложнее) Команда `/list` — список существующих комнат.

8. (Очень сложно) Перепишите на multi-threaded: thread pool обрабатывает события из общей очереди (готовых fd). Реализуйте per-client lock и общий `shared_mutex` на rooms.

## Что дальше

Глава 46 — **шифрование XOR pre-key + история на диск**. Простое шифрование сообщений (учебное — НЕ для production!) и запись истории чата в файл, чтобы новые клиенты могли её прочитать.

Глава 47 (последняя в Части V) — **сборка и деплой**: static linking, минимальный Docker образ, упаковка для дистрибуции.

После — Часть VI: C++17 как бонус. Финал книги виден.
