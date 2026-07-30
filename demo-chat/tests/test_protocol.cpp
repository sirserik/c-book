// Тесты протокола и истории чата.
// Сборка и запуск:  make tests && ./build/tests/test_protocol
#include "history.h"
#include "protocol.h"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cout << "FAIL  " << what << "\n";
    }
}

template <typename A, typename B>
void check_eq(const A& got, const B& expected, const std::string& what) {
    ++checks;
    if (!(got == expected)) {
        ++failures;
        std::cout << "FAIL  " << what << ": получили " << got
                  << ", ждали " << expected << "\n";
    }
}

// === Кодирование ===

void test_encode() {
    std::vector<std::uint8_t> f = chat::encode(chat::kHello, "Alice");
    // 4 байта длины + версия + тип + 5 байт имени
    check_eq(f.size(), static_cast<std::size_t>(11), "размер фрейма Hello('Alice')");
    check_eq(static_cast<int>(f[0]), 7, "длина в первом байте (little-endian)");
    check_eq(static_cast<int>(f[4]), static_cast<int>(chat::PROTO_VERSION), "версия");
    check_eq(static_cast<int>(f[5]), static_cast<int>(chat::kHello), "тип");

    // Пустая нагрузка допустима.
    std::vector<std::uint8_t> empty = chat::encode(chat::kLeave, "");
    check_eq(empty.size(), static_cast<std::size_t>(6), "фрейм без нагрузки — только заголовок");

    // Слишком большая нагрузка отвергается.
    bool threw = false;
    try {
        chat::encode(chat::kMessage, std::string(chat::MAX_PAYLOAD + 1, 'x'));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "нагрузка больше MAX_PAYLOAD отвергается");
}

// === Разбор ===

void test_decode_simple() {
    std::vector<std::uint8_t> acc = chat::encode(chat::kMessage, "привет");

    std::uint8_t type = 0;
    std::string payload;
    check(chat::try_decode(acc, type, payload), "целый фрейм разобран");
    check_eq(static_cast<int>(type), static_cast<int>(chat::kMessage), "тип сохранился");
    check_eq(payload, std::string("привет"), "нагрузка сохранилась");
    check(acc.empty(), "буфер опустошён после разбора");

    check(!chat::try_decode(acc, type, payload), "из пустого буфера ничего не разбирается");
}

void test_decode_partial() {
    // Собираем три фрейма подряд и скармливаем разбору ПО ОДНОМУ БАЙТУ —
    // худший случай фрагментации, который сеть вполне может устроить.
    std::vector<std::uint8_t> wire;
    std::vector<std::uint8_t> a = chat::encode(chat::kHello, "Bob");
    std::vector<std::uint8_t> b = chat::encode(chat::kMessage, "Привет, мир");
    std::vector<std::uint8_t> c = chat::encode(chat::kJoin, "general");
    wire.insert(wire.end(), a.begin(), a.end());
    wire.insert(wire.end(), b.begin(), b.end());
    wire.insert(wire.end(), c.begin(), c.end());

    std::vector<std::uint8_t> acc;
    std::vector<std::string> got;
    std::uint8_t type = 0;
    std::string payload;

    for (std::size_t i = 0; i < wire.size(); ++i) {
        acc.push_back(wire[i]);
        while (chat::try_decode(acc, type, payload)) {
            got.push_back(payload);
        }
    }

    check_eq(got.size(), static_cast<std::size_t>(3), "по одному байту собрались все три фрейма");
    if (got.size() == 3) {
        check_eq(got[0], std::string("Bob"), "первый фрейм");
        check_eq(got[1], std::string("Привет, мир"), "второй фрейм");
        check_eq(got[2], std::string("general"), "третий фрейм");
    }
    check(acc.empty(), "после разбора всего буфер пуст");
}

void test_decode_glued() {
    // Обратный случай: два фрейма пришли одним куском.
    std::vector<std::uint8_t> acc = chat::encode(chat::kHello, "Ann");
    std::vector<std::uint8_t> second = chat::encode(chat::kMessage, "два");
    acc.insert(acc.end(), second.begin(), second.end());

    std::uint8_t type = 0;
    std::string payload;
    int n = 0;
    while (chat::try_decode(acc, type, payload)) ++n;
    check_eq(n, 2, "из склеенного куска разобраны оба фрейма");
}

void test_decode_bad() {
    // Неизвестная версия.
    {
        std::vector<std::uint8_t> acc = chat::encode(chat::kHello, "X");
        acc[4] = 99;
        std::uint8_t type = 0;
        std::string payload;
        bool threw = false;
        try { chat::try_decode(acc, type, payload); }
        catch (const std::runtime_error&) { threw = true; }
        check(threw, "чужая версия протокола отвергается");
    }
    // Абсурдная длина.
    {
        std::vector<std::uint8_t> acc;
        acc.push_back(0xFF); acc.push_back(0xFF); acc.push_back(0xFF); acc.push_back(0xFF);
        acc.push_back(1); acc.push_back(1);
        std::uint8_t type = 0;
        std::string payload;
        bool threw = false;
        try { chat::try_decode(acc, type, payload); }
        catch (const std::runtime_error&) { threw = true; }
        check(threw, "длина больше допустимой отвергается до выделения памяти");
    }
    // Длина меньше минимальной (нет места под версию и тип).
    {
        std::vector<std::uint8_t> acc;
        acc.push_back(1); acc.push_back(0); acc.push_back(0); acc.push_back(0);
        acc.push_back(1);
        std::uint8_t type = 0;
        std::string payload;
        bool threw = false;
        try { chat::try_decode(acc, type, payload); }
        catch (const std::runtime_error&) { threw = true; }
        check(threw, "слишком короткий фрейм отвергается");
    }
}

// === История ===

chat::HistoryRecord rec(const std::string& name, const std::string& room,
                        const std::string& text) {
    chat::HistoryRecord r;
    r.timestamp = 1000;
    r.name = name;
    r.room = room;
    r.text = text;
    return r;
}

void test_history() {
    const char* path = "/tmp/chat_test_history.log";
    ::remove(path);

    {
        chat::History h(path);
        h.append(rec("alice", "general", "первое"));
        h.append(rec("bob", "general", "второе"));
        h.append(rec("alice", "secret", "тайное"));
        h.append(rec("carol", "general", "третье"));
    }
    {
        chat::History h(path);
        std::vector<chat::HistoryRecord> last = h.recent("general", 2);
        check_eq(last.size(), static_cast<std::size_t>(2), "recent(2) вернул две записи");
        if (last.size() == 2) {
            check_eq(last[0].name, std::string("bob"), "предпоследняя запись комнаты");
            check_eq(last[1].text, std::string("третье"), "последняя запись комнаты");
        }

        std::vector<chat::HistoryRecord> all = h.recent("", 100);
        check_eq(all.size(), static_cast<std::size_t>(4),
                 "без фильтра по комнате возвращаются все записи");

        std::vector<chat::HistoryRecord> secret = h.recent("secret", 10);
        check_eq(secret.size(), static_cast<std::size_t>(1),
                 "фильтр по комнате отбирает только свои сообщения");
    }

    ::remove(path);
}

}  // namespace

int main() {
    test_encode();
    test_decode_simple();
    test_decode_partial();
    test_decode_glued();
    test_decode_bad();
    test_history();

    std::cout << (failures ? "ЕСТЬ ОШИБКИ: " : "все проверки прошли: ")
              << (checks - failures) << "/" << checks << "\n";
    return failures ? 1 : 0;
}
