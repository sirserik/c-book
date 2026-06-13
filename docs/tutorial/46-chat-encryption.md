# Глава 46. Шифрование XOR pre-key + история на диск

В этой главе две темы:
1. **Шифрование** — пользователь не хочет, чтобы провайдер/сосед видел его сообщения. Покажем XOR pre-key (**учебный** способ; **категорически не подходит для production**!) и обсудим, что делает TLS.
2. **История на диск** — чтобы клиент после перезапуска сервера или подключения позже увидел свежие сообщения комнаты.

Это **предпоследняя** глава Части V. После — финал части (сборка/деплой) и Часть VI про C++17 как бонус.

## XOR pre-key — учебный пример

Алгоритм:

```cpp
encrypt(plain, key):
    for i in 0..len(plain):
        cipher[i] = plain[i] XOR key[i % len(key)]
```

`XOR` — побитовая операция «исключающее или». Свойство: `A XOR B XOR B == A`. То есть **decrypt — тот же алгоритм** с тем же ключом.

```cpp
void xor_in_place(std::string& data, const std::string& key) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] ^= key[i % key.size()];
    }
}
```

Если ключ длиннее plaintext'а — каждый байт XOR'ится со своим. Если короче — ключ повторяется (repeating key).

Пример:

```
plain:    Hello, this is a confidential message!
cipher:   3b 00 0f 1e 0a 58 11 46 5b 1a 16 43 ...
decrypted: Hello, this is a confidential message!
```

Encrypt → cipher выглядит как мусор. Decrypt → возвращает оригинал.

### Почему XOR pre-key **сломан**

Несколько атак:

**1. Известный plaintext (known-plaintext attack).** Если злоумышленник знает **часть** plaintext'а (например, что сообщения начинаются с «Hello»), он восстанавливает ключ:

```cpp
known = "Hello, "
cipher = ... (известно из перехвата)
key = known XOR cipher[:7]
```

В нашем демо:
```
Знаем начало 'Hello, '.
XOR с cipher[:7] даёт: 'secret1'
Это первые 7 байт реального ключа!
```

Имея ключ, дешифруем **остальное**.

**2. Repeated key + статистика.** Если ключ короче plaintext'а и повторяется, атакующий собирает много шифровок одного ключа. Через анализ частот букв (буква «о» в русском самая частая) восстанавливает ключ. Это классическая криптоанализ Виженера, известная **с 1854 года**.

**3. XOR-сравнение шифровок.** Если две разные plaintext'а зашифрованы **одним** ключом:
```
cipher1 XOR cipher2 == plain1 XOR plain2
```
Если plain1 короче plain2 — оставшаяся часть = plain2 XOR (повтор ключа), что даёт частичную информацию.

**Вывод**: XOR pre-key — **не криптография**. Не используйте для реальной защиты. Это **обфускация** — спрятать данные от случайного просмотра, не от атакующего.

## Что делает настоящая криптография

Современные шифры (AES, ChaCha20) обходят все XOR-атаки:

- **Замена и перестановка** битов в каждом блоке (S-boxes).
- **Несколько раундов** для diffusion (один бит plaintext влияет на много бит cipher).
- **Initialization vector (IV)** — случайные данные при каждом шифровании, чтобы один plaintext дал разные cipher'ы.
- **AEAD** (authenticated encryption) — встроенная проверка целостности (MAC).

Реализовывать **своё** шифрование в production — катастрофическая ошибка. «Don't roll your own crypto». Используйте проверенные библиотеки.

## TLS — стандарт для сетевого шифрования

**TLS** (Transport Layer Security) — стандарт защиты TCP-соединений. HTTPS = HTTP + TLS. Тот же TLS для chat/email/database протоколов.

Что делает TLS:

1. **Handshake**: клиент и сервер договариваются о ключах через **Diffie-Hellman** (или вариант).
2. **Аутентификация сервера** через **X.509 сертификат** (Let's Encrypt и др.).
3. **Симметричное шифрование** (AES-GCM или ChaCha20-Poly1305) данных в обе стороны.
4. **MAC** на каждый фрейм — обнаружение tampering.
5. **Replay protection** — повторное использование старых пакетов не работает.

Главная фишка — **forward secrecy**: даже если ключ скомпрометируют завтра, **старые** перехваченные сообщения нельзя расшифровать. Достигается через **ephemeral Diffie-Hellman**: ключи создаются на сессию, потом уничтожаются.

### Diffie-Hellman вкратце

Алиса и Боб хотят общий секрет, общаясь через **публичный канал** (где подслушивают).

1. Договариваются о публичных параметрах p (простое) и g (генератор).
2. Алиса: выбирает секретный `a`, шлёт Бобу `A = g^a mod p`.
3. Боб: выбирает секретный `b`, шлёт Алисе `B = g^b mod p`.
4. Алиса: вычисляет `S = B^a mod p`.
5. Боб: вычисляет `S = A^b mod p`.
6. **`S` одинаков** у обоих, но третья сторона его получить не может.

Это **математическое чудо**: подслушав `A` и `B`, нельзя из них восстановить `S` без `a` или `b` (задача дискретного логарифмирования, NP-hard).

Современные варианты — **ECDH** на эллиптических кривых (Curve25519): те же гарантии, меньше ключи, быстрее.

### TLS в коде

Самостоятельная реализация TLS — тысячи строк сложной криптографии. Используют:

- **OpenSSL** — индустриальный стандарт, C API.
- **BoringSSL** — fork от Google.
- **mbedTLS** — для embedded.
- **wolfSSL** — небольшой коммерческий.

В C++ обёртки: **`boost::asio::ssl`**, **`cpp-httplib`** (HTTPS клиент), **`Poco::Net::SecureStreamSocket`**.

Подключение TLS к нашему чату — это:
1. `SSL_CTX_new` — создать контекст.
2. Загрузить сертификат сервера.
3. Заменить `recv`/`send` на `SSL_read`/`SSL_write`.
4. Сделать handshake при accept.

Не сложно, но за рамками книги. Если делаете production — изучайте OpenSSL.

## История на диск

Когда клиент подключается **в существующую** комнату, он не видел сообщений до этого. Решение — **scrollback**: при join клиенту шлют **последние N** сообщений из истории.

Это требует **персистентности** — записывать каждое сообщение в файл.

### Формат

Простой append-only бинарный лог (как наш WAL из главы 35):

```
Каждая запись:
  [4]   length u32 LE
  [4]   timestamp u32 LE (Unix epoch)
  [2]   name_len u16 LE
  [2]   room_len u16 LE
  [2]   text_len u16 LE
  [...] name (utf-8)
  [...] room (utf-8)
  [...] text (utf-8)
```

Похоже на наш framing protocol (глава 44), но field-based — для удобства десериализации.

### History класс

```cpp
class History {
public:
    explicit History(const std::string& path);
    ~History();

    void append(const HistoryRecord& r);
    std::vector<HistoryRecord> recent(const std::string& room, std::size_t n);

private:
    std::string path_;
    int fd_;
};
```

`append` — добавить запись + fsync (как WAL).

`recent(room, n)` — последние N записей для комнаты. Простая реализация: читать **весь файл**, фильтровать по `room`, оставлять последние n.

### Реализация append

```cpp
void History::append(const HistoryRecord& r) {
    std::vector<std::uint8_t> body;
    w_u32(body, r.timestamp);
    w_u16(body, r.name.size());
    w_u16(body, r.room.size());
    w_u16(body, r.text.size());
    body.insert(body.end(), r.name.begin(), r.name.end());
    body.insert(body.end(), r.room.begin(), r.room.end());
    body.insert(body.end(), r.text.begin(), r.text.end());

    std::vector<std::uint8_t> framed;
    w_u32(framed, body.size());
    framed.insert(framed.end(), body.begin(), body.end());

    write_all(fd_, framed);
    fsync(fd_);
}
```

Сначала собрали body, обернули в `[length:4][body]`, записали + fsync.

`O_APPEND` при open — атомарность write на конец файла даже при concurrent writers.

### Реализация recent

```cpp
std::vector<HistoryRecord> History::recent(const std::string& room, std::size_t n) {
    std::vector<HistoryRecord> all;
    int rfd = ::open(path_.c_str(), O_RDONLY);
    
    std::uint8_t hdr[4];
    while (read_all(rfd, hdr, 4)) {
        std::uint32_t body_len = r_u32(hdr);
        std::vector<std::uint8_t> body(body_len);
        if (!read_all(rfd, body.data(), body_len)) break;
        
        HistoryRecord rec;
        // ... парсим поля ...
        
        if (room.empty() || rec.room == room) {
            all.push_back(std::move(rec));
        }
    }
    ::close(rfd);
    
    if (all.size() > n) {
        all.erase(all.begin(), all.end() - n);
    }
    return all;
}
```

Linear scan всего файла. На миллионах сообщений — медленно.

**Production-решения**:
- **Индексация по room**: отдельные файлы или B+tree (как в нашей мини-СУБД!).
- **Log rotation**: каждый день — свой файл. Recent читает только последние.
- **In-memory cache**: последние N сообщений каждой комнаты в RAM.
- **Базу данных** (PostgreSQL/MongoDB) вместо своего формата.

В нашем учебном — простой scan.

## Демо

```bash
$ ./build/history_demo
=== Последние 10 в general ===
[1778530684] [general] <Alice> первое сообщение
[1778530685] [general] <Bob> ответ
[1778530687] [general] <Alice> ещё одно

=== Все записи ===
[1778530684] [general] <Alice> первое сообщение
[1778530685] [general] <Bob> ответ
[1778530686] [tech] <Carol> что-то про код
[1778530687] [general] <Alice> ещё одно
```

История сохранилась, фильтр по комнате работает.

## Интеграция в chat-server

В `rooms_chat_server.cpp` добавляем:

```cpp
chat::History history("data/chat_history.bin");

// При Message:
case chat::kMessage: {
    chat::HistoryRecord r;
    r.timestamp = std::time(nullptr);
    r.name = c.name;
    r.room = c.room;
    r.text = payload;
    history.append(r);
    broadcast_room(...);
    break;
}

// При Join:
case chat::kJoin: {
    // ... установка room ...
    auto recent = history.recent(c.room, 20);
    for (const auto& msg : recent) {
        enqueue_frame(c, chat::kMessage,
                      "<" + msg.name + "> " + msg.text);
    }
    break;
}
```

Новый клиент при входе в комнату сразу получает последние 20 сообщений. Опыт **намного** ближе к настоящему чату.

**Опасность**: history.append + fsync **блокирует**. На реакторе — застопорит всех клиентов на миллисекунды. Решения:
- **Batch append**: накопить N сообщений, один fsync.
- **Async writer thread**: основной поток отдаёт сообщение в очередь, отдельный поток пишет.
- **Background flush** — fsync раз в секунду, не на каждое сообщение (потеря 1 сек при крахе).

В нашем коде — простой синхронный append. Учебное.

## End-to-end encryption — обзор

Текущая модель: сервер видит сообщения **в открытом виде** (даже с TLS — TLS только защищает канал клиент↔сервер, не от самого сервера).

**E2E**: сервер видит только зашифрованные blob'ы, не текст. Только адресат может расшифровать.

Используется в **Signal**, **WhatsApp** (2014+), **iMessage**, **Telegram secret chats**. Алгоритмы:

- **X3DH** для handshake — обмен ключами через сервер, но без раскрытия секретов серверу.
- **Double Ratchet** для постоянной ротации ключей. Forward secrecy + future secrecy.
- **Out-of-band verification** — пользователи сверяют отпечатки ключей (QR код).

Реализовать E2E **самому** — почти невозможно сделать правильно. Используют **libsignal-protocol** или **libolm** (для Matrix).

Что **проигрывает** server-side в E2E:
- Поиск по истории — нельзя (сервер не видит текста).
- Multi-device — сложнее (каждое устройство — свой ключ).
- Push с предпросмотром — нет полного текста в notification.

Trade-off приватность vs функциональность.

## Главные правила главы

1. **Не пишите свой криптоалгоритм для production.** Используйте AES/ChaCha20.
2. **XOR pre-key — обфускация, не шифрование.**
3. **TLS для сетевой защиты** — OpenSSL/BoringSSL.
4. **Diffie-Hellman** для согласования ключей без раскрытия в канале.
5. **Forward secrecy** — даже компрометация ключа не раскрывает старые сообщения.
6. **append-only лог** для истории — durable, простой.
7. **fsync на каждое сообщение блокирует** — batch или async writer для масштаба.
8. **E2E через libsignal**, не самостоятельно. Сложно.

## Маленькое упражнение

1. Запустите `./build/xor_demo`. Понаблюдайте known-plaintext attack — ключ восстанавливается из 7 байт известного начала.

2. Запустите `./build/history_demo`. Откройте `data/chat_history.bin` через `xxd` — изучите формат.

3. Сделайте `history.append` 1000 раз в цикле. Засеките время. fsync — узкое место?

4. (Сложнее) Интегрируйте `History` в `rooms_chat_server`: записывать каждое Message, при Join отправлять recent 20.

5. (Сложнее) Замените XOR на **AES-GCM** из OpenSSL. Подключите `-lcrypto`, прочитайте `man EVP_EncryptInit_ex`.

6. (Сложнее) Реализуйте простой **per-room index**: при append кроме записи в общий лог писать смещение в `index_<room>.bin`. Recent читает по индексу — быстрее.

7. (Очень сложно) Прочитайте Signal Protocol whitepaper. Реализуйте упрощённый Double Ratchet (без X3DH).

8. (Очень сложно) Подключите OpenSSL к chat-server: TLS handshake при accept, потом SSL_read/SSL_write вместо recv/send.

## Что дальше

Глава 47 — **финал Части V**: сборка и упаковка. Static linking, минимальный Docker, что нужно для деплоя сервера в облако. Подведём итог.

После — Часть VI: C++17 как бонус (optional, variant, filesystem, string_view, ranges/concepts/coroutines обзор). Книга подходит к финишу.
