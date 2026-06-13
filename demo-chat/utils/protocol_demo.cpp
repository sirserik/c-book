// Демо бинарного протокола: encode → split на куски → decode.
// Проверяет, что парсер корректно работает с частичными reads (как реальный TCP).

#include "protocol.h"

#include <iomanip>
#include <iostream>
#include <vector>

void hexdump(const std::vector<std::uint8_t>& buf) {
    for (std::size_t i = 0; i < buf.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(buf[i]) << " ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n";
}

int main() {
    // 1) Кодируем три фрейма.
    auto f1 = chat::encode(chat::kHello, "Alice");
    auto f2 = chat::encode(chat::kMessage, "Привет всем!");
    auto f3 = chat::encode(chat::kJoin, "general");

    std::cout << "=== Закодированные фреймы ===\n";
    std::cout << "Hello 'Alice' (" << f1.size() << " байт):\n"; hexdump(f1);
    std::cout << "Message 'Привет всем!' (" << f2.size() << " байт):\n"; hexdump(f2);
    std::cout << "Join 'general' (" << f3.size() << " байт):\n"; hexdump(f3);

    // 2) Склеиваем в один поток и режем на куски разного размера (как TCP).
    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), f1.begin(), f1.end());
    stream.insert(stream.end(), f2.begin(), f2.end());
    stream.insert(stream.end(), f3.begin(), f3.end());

    std::cout << "\n=== Декодирование с фрагментацией ===\n";

    std::vector<std::uint8_t> acc;
    std::size_t pos = 0;
    // Размеры чанков, имитирующие short reads.
    std::vector<std::size_t> chunk_sizes{3, 10, 1, 7, 4, 50};
    std::size_t ci = 0;

    while (pos < stream.size()) {
        std::size_t chunk = chunk_sizes[ci % chunk_sizes.size()];
        ++ci;
        chunk = std::min(chunk, stream.size() - pos);
        acc.insert(acc.end(), stream.begin() + pos, stream.begin() + pos + chunk);
        pos += chunk;

        std::cout << "[чанк " << chunk << " байт, в буфере " << acc.size() << "]\n";

        // Пробуем извлечь все доступные фреймы.
        std::uint8_t type;
        std::string payload;
        while (chat::try_decode(acc, type, payload)) {
            const char* tname = "?";
            switch (type) {
                case chat::kHello:   tname = "Hello"; break;
                case chat::kMessage: tname = "Message"; break;
                case chat::kJoin:    tname = "Join"; break;
                default: break;
            }
            std::cout << "  → " << tname << ": '" << payload << "'\n";
        }
    }

    std::cout << "\nОстаток в буфере: " << acc.size() << " байт\n";
    return 0;
}
