#ifndef CHAT_HISTORY_H
#define CHAT_HISTORY_H

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace chat {

// История чата в append-only файле. Формат записи (как в WAL):
//   [4]   length u32 LE = 4 + 2 + 2 + 2 + name.size() + room.size() + text.size()
//   [4]   timestamp u32 LE (секунды от epoch)
//   [2]   name_len u16 LE
//   [2]   room_len u16 LE
//   [2]   text_len u16 LE
//   [...] name + room + text (по len байтов)
//
// Чтение — последовательное; перемотка к концу для recent-N через scan.

struct HistoryRecord {
    std::uint32_t timestamp;
    std::string name;
    std::string room;
    std::string text;
};

class History {
public:
    explicit History(const std::string& path);
    ~History();

    // Append + fsync (durable).
    void append(const HistoryRecord& r);

    // Прочитать последние N записей для комнаты (или все, если room пуст).
    // Простая реализация — читаем всё, фильтруем, держим last N. Для миллионов
    // записей нужен индекс или log-rotation.
    std::vector<HistoryRecord> recent(const std::string& room, std::size_t n);

private:
    std::string path_;
    int fd_;
};

}  // namespace chat

#endif
