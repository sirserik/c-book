#ifndef DB_WAL_H
#define DB_WAL_H

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace db {

class WALError : public std::runtime_error {
public:
    explicit WALError(const std::string& m) : std::runtime_error(m) {}
};

// Write-Ahead Log. Простой append-only журнал записей вида (key, value).
//
// Layout файла — последовательность записей:
//   u32 length    (длина полезной нагрузки, всегда 17)
//   u8  type      (1 = insert)
//   i64 key
//   i64 value
//   u32 crc       (от type+key+value)
//
// Итого 25 байт на запись.
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    // Записать insert-операцию. Вызывает fsync — durable до возврата.
    void log_insert(std::int64_t key, std::int64_t value);

    // Прочитать все записи и вызвать колбэк. Битые записи (плохая crc, обрыв
    // в конце файла) останавливают replay — это означает «после этой точки
    // данные не были durable».
    void replay(std::function<void(std::int64_t, std::int64_t)> cb);

    // Очистить журнал (после checkpoint).
    void truncate();

    // Сколько байт сейчас в WAL — для тестов.
    std::size_t size_on_disk() const;

private:
    std::string path_;
    int fd_;
};

}  // namespace db

#endif
