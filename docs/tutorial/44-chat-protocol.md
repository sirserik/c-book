# Глава 44. Свой бинарный протокол

До этой главы наш чат пользовался **текстовым** протоколом: «строка до `\n` — одно сообщение». Просто, но хрупко:

- Что если в сообщении есть `\n` (multi-line текст)?
- Как различать «обычное сообщение» и «join room»?
- Как версионировать? Старый клиент шлёт старый формат серверу с новым.

Решение — **бинарный фрейм-протокол**. Каждое сообщение — фрейм с **явной длиной** и **типом**. Никакой неоднозначности. Это то, что делают HTTP/2, WebSocket, gRPC, MongoDB wire protocol, и большинство production-протоколов.

В этой главе спроектируем и реализуем простой фрейм-формат. Плюс **аккумулятор** для частичных reads (TCP — поток, не пакеты!).

## Зачем фрейминг

TCP даёт **поток байтов**, не сообщения. Если клиент сделал `send("hello")`, `send("world")` — сервер может получить:
- За один `recv`: `"helloworld"`.
- За два `recv`: `"hello"` + `"world"`.
- За пять `recv`: `"he"` + `"l"` + `"low"` + `"or"` + `"ld"`.

Как сервер узнает, где кончается одно сообщение и начинается другое? **Нужен делимитер**.

Варианты:
1. **Делимитер-символ** (`\n`, `\0`). Просто, но что если он встретится в данных? Бинарные данные ломают.
2. **Длина-префикс** (length-prefixed): сначала длина, потом столько байтов. Самый чистый.
3. **Фиксированный размер** сообщений — подходит редко.

В чате нам нужны произвольные строки + разные типы сообщений. **Length-prefixed + type-byte** — стандарт.

## Формат фрейма

```
[0..3]   u32 LE length  (= version + type + payload, т.е. всё после length)
[4]      u8  version
[5]      u8  type
[6..]    payload (length - 2 байт)
```

Минимальный фрейм — `length(4) + version(1) + type(1) + payload(0)` = 6 байт.

`length` хранится в **little-endian** (как договорились в главе 32). Все стороны должны знать порядок — мы фиксируем LE.

`version` — текущая версия протокола. У нас 1. Если в будущем формат поменяется — версия инкрементится. Клиент с v1 → сервер с v2 поймёт по версии: нужно отказывать или fallback.

`type` — что за сообщение. У нас:
- 1 = Hello (клиент представляется)
- 2 = Message (текст в чате)
- 3 = Notify (серверное уведомление)
- 4 = Join (войти в комнату)
- 5 = Leave (выйти)

`payload` — переменной длины, до 64 KiB (MAX_PAYLOAD). Лимит защищает от plain malicious клиентов, шлющих гигабайтные «длины».

## Encoder

`src/protocol.cpp`:

```cpp
std::vector<std::uint8_t> encode(std::uint8_t type, const std::string& payload) {
    if (payload.size() > MAX_PAYLOAD) {
        throw std::runtime_error("payload too large");
    }
    std::vector<std::uint8_t> out;
    std::uint32_t len = static_cast<std::uint32_t>(2 + payload.size());
    out.reserve(4 + len);
    write_u32_le(out, len);
    out.push_back(PROTO_VERSION);
    out.push_back(type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
```

Простой. Считаем length, пишем 4 байта LE, потом version+type+payload.

`write_u32_le` — как в главе 32:

```cpp
void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    }
}
```

## Decoder и аккумулятор

`recv` возвращает произвольное количество байт. Может быть **половина** одного фрейма. Может быть **полтора** фрейма (целый + начало второго).

Решение — **аккумулятор**: `std::vector<uint8_t>`, в который складываем всё пришедшее. На каждом recv:

1. Дописать данные в аккумулятор.
2. **В цикле** пробовать извлечь фрейм. Если получилось — обработать, продолжить цикл. Если данных не хватает — выйти и ждать ещё.

```cpp
bool try_decode(std::vector<std::uint8_t>& acc,
                std::uint8_t& out_type,
                std::string& out_payload) {
    if (acc.size() < 4) return false;             // нет даже длины
    std::uint32_t len = read_u32_le(acc.data());
    if (len < 2 || len > MAX_PAYLOAD + 2) {
        throw std::runtime_error("invalid length");
    }
    if (acc.size() < 4 + len) return false;       // длина есть, тело ещё не пришло

    std::uint8_t version = acc[4];
    if (version != PROTO_VERSION) {
        throw std::runtime_error("unsupported version");
    }
    out_type = acc[5];
    out_payload.assign(reinterpret_cast<const char*>(acc.data() + 6), len - 2);

    acc.erase(acc.begin(), acc.begin() + 4 + len);
    return true;
}
```

Три исхода:
1. **Данных хватает на фрейм** — извлечь, потребить байты, вернуть `true`.
2. **Длину видим, но тела нет** — `false`. Ждём ещё.
3. **Длины нет (< 4 байт)** — `false`.

При **повреждении** (нереальная длина, плохая версия) — `throw`. Это означает «соединение испорчено», caller должен закрыть клиента.

### Шаблон использования

```cpp
char buf[1024];
ssize_t r = recv(fd, buf, sizeof(buf), 0);
acc.insert(acc.end(), buf, buf + r);

uint8_t type;
std::string payload;
try {
    while (try_decode(acc, type, payload)) {
        handle(type, payload);
    }
} catch (const std::exception& e) {
    log("протокол сломан: ", e.what());
    close(fd);
}
```

Идиома **«читай в буфер, потом извлекай все доступные фреймы»**. Работает в любой архитектуре — thread-per-conn, реактор, async.

## Альтернатива — кольцевой буфер

`std::vector::erase` с начала — O(N) (сдвигает все остальные элементы). На больших объёмах данных — медленно.

Решение — **кольцевой буфер** (ring buffer): фиксированный массив + два индекса (read_pos, write_pos). Read/write — O(1).

```cpp
class RingBuffer {
    std::vector<uint8_t> data_;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    size_t size_ = 0;
};
```

Для **большого трафика** (мегабайты в секунду) — обязательно. Для нашего чата — `vector` достаточно.

Boost.Asio использует свой `streambuf`, Node.js — внутренние Buffer'ы, ниже уровень — ring buffers.

## Версионирование

Что делать при **изменении** протокола?

**Минорные**: добавили новый тип сообщения (типа 6). Старый клиент шлёт сообщения типов 1-5 — сервер v2 их понимает. Новый клиент шлёт 6 — старый сервер падает или игнорирует.

**Мажорные**: поменялось значение существующего поля. Например, `length` стало 8 байт. Старый клиент с v1 → сервер v2 не разберёт.

**Стратегии**:

1. **Bump major version, отказ старым.** Грубо, но просто.
2. **Negotiation**: при handshake клиент шлёт «поддерживаю v1, v2, v3», сервер отвечает «давай v2».
3. **Forward compatibility**: новые поля **в конце**. Старый клиент игнорирует «лишние» байты.

В нашем протоколе — bump version на каждое изменение. Учебное упрощение.

## Демо

`utils/protocol_demo.cpp` показывает round-trip с фрагментацией:

```bash
$ ./build/protocol_demo
=== Закодированные фреймы ===
Hello 'Alice' (11 байт):
07 00 00 00 01 01 41 6c 69 63 65 
   ^^^^^^^^^^^                              length=7 (LE)
               ^^                            version=1
                  ^^                         type=1 (Hello)
                     ^^^^^^^^^^^^^^^^        "Alice" (ASCII)

Message 'Привет всем!' (28 байт):
18 00 00 00 01 02 d0 9f d1 80 d0 b8 d0 b2 d0 b5 
d1 82 20 d0 b2 d1 81 d0 b5 d0 bc 21 
                  ^^ "П" в UTF-8 = 0xD0 0x9F (2 байта)

=== Декодирование с фрагментацией ===
[чанк 3 байт, в буфере 3]                  ← length ещё не полностью пришла
[чанк 10 байт, в буфере 13]
  → Hello: 'Alice'                          ← извлечён фрейм 1
[чанк 1 байт, в буфере 3]
[чанк 7 байт, в буфере 10]
[чанк 4 байт, в буфере 14]
[чанк 27 байт, в буфере 41]
  → Message: 'Привет всем!'                 ← фрейм 2 + начало 3 в одном чанке
  → Join: 'general'                         ← фрейм 3 извлечён сразу
Остаток в буфере: 0 байт
```

Decoder корректно работает при произвольной фрагментации. Это критично для TCP.

## Тонкости в production

### Frame compression

Большие сообщения можно **сжимать** перед отправкой. Добавить bit в header: «payload сжат через gzip/zstd». Decoder декомпрессирует.

WebSocket делает это через `permessage-deflate` extension.

### TLS / шифрование на уровне фрейма

Шифровать **payload** каждого фрейма отдельным ключом. Или шифровать **весь** TCP-поток через TLS (правильнее). О шифровании — глава 46.

### Heartbeat / keepalive

Длинно-висящие соединения теряются (NAT timeout, network glitch). Решение — периодический «ping» фрейм с ответом «pong». Если не пришёл за N секунд — закрываем.

WebSocket: control frames `0x09` (ping) / `0x0A` (pong).

### Big-endian для сетевых протоколов

Исторически сетевые протоколы используют **big-endian** (`htons`/`htonl`). Мы выбрали LE, потому что:
- Современные процессоры (x86, ARM) — LE.
- Наш клиент и сервер — на одной архитектуре.
- Нет лишних htons/ntohs преобразований.

Если бы делали **переносимый** протокол (Java-клиент, например), стоило бы выбрать BE — стандарт всё-таки.

### Schema evolution: protobuf, FlatBuffers, MessagePack

Для **сложного** контента (нестабильная схема, много полей) есть готовые форматы:

- **Protocol Buffers** (Google) — schema-based, optional fields, forward/backward compat.
- **FlatBuffers** — zero-copy, mmap-able.
- **MessagePack** — JSON-like, но binary.
- **Cap'n Proto** — то же.

Production-протоколы обычно **не** делают свой бинарный формат, а используют готовые. Они решают версионирование, имеют клиенты на 30 языках, проверены временем.

Наш «свой» формат — учебный. На реальном проекте — protobuf.

## Демо использования в чате

Старый чат шёл строками. Новый — фреймами:

```cpp
// Клиент: представляется
std::vector<uint8_t> hello = chat::encode(chat::kHello, "Alice");
send(fd, hello.data(), hello.size(), 0);

// Клиент: шлёт сообщение
auto msg = chat::encode(chat::kMessage, "Привет!");
send(fd, msg.data(), msg.size(), 0);

// Сервер: получает
char buf[1024];
ssize_t r = recv(fd, buf, sizeof(buf), 0);
acc.insert(acc.end(), buf, buf + r);

uint8_t type;
std::string payload;
while (chat::try_decode(acc, type, payload)) {
    if (type == chat::kHello) {
        client.name = payload;
    } else if (type == chat::kMessage) {
        broadcast("<" + client.name + "> " + payload + "\n");
    }
}
```

Никакой неоднозначности. Сообщения с переносами строки внутри — нормально. Бинарные blob'ы — тоже.

В главе 45 мы интегрируем это с реактор-сервером для multi-room функционала.

## Главные правила главы

1. **TCP = поток, не пакеты.** Делите сами через фрейминг.
2. **Length-prefixed framing** — стандартный подход. Длина в начале, тело после.
3. **Версионирование с первой версии.** Потом поздно.
4. **Лимит на размер payload.** Иначе DoS-атакой убьют память.
5. **Аккумулятор для частичных reads.** `std::vector<uint8_t>` + `try_decode` в цикле.
6. **Кольцевой буфер для масштаба** — O(1) операции.
7. **Production protobuf/FlatBuffers/MessagePack**, не своё. Если только не нужна абсолютная компактность.
8. **TLS поверх TCP** для безопасности, не своё шифрование (см. главу 46).

## Маленькое упражнение

1. Запустите `./build/protocol_demo`. Изучите hex-вывод, найдите length-байты, version, type, payload.

2. Измените `MAX_PAYLOAD` на 100, попробуйте закодировать строку 200 байт. Что произойдёт?

3. Подайте в `try_decode` намеренно битые данные: length=0xFFFFFFFF. Что вернёт?

4. Реализуйте `encode_binary(type, vector<uint8_t>& payload)` — для бинарных данных, не только string.

5. (Сложнее) Перепишите reactor_chat_server с использованием нашего framing protocol. Сервер ждёт kHello, потом kMessage'и.

6. (Сложнее) Реализуйте ping/pong: каждые 30 секунд сервер шлёт kPing, клиент отвечает kPong. Без ответа в 60 секунд — disconnect.

7. (Сложнее) Реализуйте `RingBuffer` класс с O(1) push/pop. Замените `std::vector` accumulator на него.

8. (Очень сложно) Подключите protobuf. Опишите `.proto` файл для наших сообщений, сгенерируйте C++ код, замените наш encode/try_decode.

## Что дальше

Глава 45 — **многокомнатный чат**. Используем наш framing-протокол + реактор. `Room` структура, `unordered_map<string, set<int>>`. Команды `/join`, `/leave`, broadcast только в текущую комнату.

Дальше — шифрование XOR pre-key + история на диск (46), сборка-deployment (47). Часть V на финишной прямой.
