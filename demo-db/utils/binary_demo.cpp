// binary_demo — показывает, как ложатся в память структуры и как выглядит
// явная сериализация из include/binary.h.
//
// Запуск: ./build/binary_demo

#include "binary.h"

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Row {
    std::uint32_t id;
    std::uint16_t age;
    std::uint64_t balance;
};

#if defined(__GNUC__) || defined(__clang__)
struct __attribute__((packed)) RowPacked {
    std::uint32_t id;
    std::uint16_t age;
    std::uint64_t balance;
};
#endif

void hexdump(const unsigned char* data, std::size_t len) {
    for (std::size_t i = 0; i < len; i += 16) {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << i << "  ";

        for (std::size_t j = 0; j < 16; ++j) {
            if (i + j < len) {
                std::cout << std::setw(2) << std::setfill('0')
                          << static_cast<int>(data[i + j]) << " ";
            } else {
                std::cout << "   ";
            }
            if (j == 7) std::cout << " ";
        }

        std::cout << " |";
        for (std::size_t j = 0; j < 16 && i + j < len; ++j) {
            unsigned char c = data[i + j];
            std::cout << static_cast<char>((c >= 32 && c < 127) ? c : '.');
        }
        std::cout << "|\n";
    }
    std::cout << std::dec << std::setfill(' ');
}

}  // namespace

int main() {
    std::cout << "=== Как компилятор разложил Row ===\n";
    std::cout << "sizeof(Row)              = " << sizeof(Row) << "\n";
    std::cout << "alignof(Row)             = " << alignof(Row) << "\n";
    std::cout << "offsetof(Row, id)        = " << offsetof(Row, id) << "\n";
    std::cout << "offsetof(Row, age)       = " << offsetof(Row, age) << "\n";
    std::cout << "offsetof(Row, balance)   = " << offsetof(Row, balance) << "\n";
#if defined(__GNUC__) || defined(__clang__)
    std::cout << "sizeof(RowPacked)        = " << sizeof(RowPacked) << "\n";
#endif

    std::cout << "\n=== Мусор в padding-байтах ===\n";
    {
        // Заполняем стек мусором, потом кладём туда Row и смотрим на дыру.
        unsigned char noise[sizeof(Row)];
        for (std::size_t i = 0; i < sizeof(noise); ++i) noise[i] = 0xAB;

        Row r;
        std::memcpy(&r, noise, sizeof(noise));   // «грязная» память
        r.id = 42;
        r.age = 25;
        r.balance = 1000;

        const unsigned char* raw = reinterpret_cast<const unsigned char*>(&r);
        hexdump(raw, sizeof(Row));
        std::cout << "байты 6 и 7 — padding, их никто не инициализировал\n";
    }

    std::cout << "\n=== Явная сериализация ===\n";
    {
        std::vector<unsigned char> buf;
        db::write_u32(buf, 0x12345678u);
        db::write_u64(buf, 0xCAFEBABEDEADBEEFull);
        db::write_string(buf, "hello");

        std::cout << "записано " << buf.size() << " байт\n";
        hexdump(buf.data(), buf.size());

        std::cout << "\nчитаем обратно:\n";
        std::cout << "  u32 = 0x" << std::hex << db::read_u32(buf.data()) << std::dec << "\n";
        std::cout << "  u64 = 0x" << std::hex << db::read_u64(buf.data() + 4) << std::dec << "\n";
        std::size_t consumed = 0;
        std::cout << "  str = \"" << db::read_string(buf.data() + 12, consumed)
                  << "\" (" << consumed << " байт)\n";
    }

    std::cout << "\n=== Отрицательные числа ===\n";
    {
        std::vector<unsigned char> buf;
        db::write_u64(buf, static_cast<std::uint64_t>(static_cast<std::int64_t>(-1)));
        db::write_u64(buf, static_cast<std::uint64_t>(
                               static_cast<std::int64_t>(-9223372036854775807LL - 1)));
        hexdump(buf.data(), buf.size());
        std::int64_t back = static_cast<std::int64_t>(db::read_u64(buf.data()));
        std::cout << "прочитали обратно: " << back << "\n";
    }

    return 0;
}
