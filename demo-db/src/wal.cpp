#include "wal.h"
#include "binary.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace db {

namespace {

constexpr std::size_t PAYLOAD_BYTES = 1 + 8 + 8;   // type+key+value
constexpr std::size_t RECORD_BYTES  = 4 + PAYLOAD_BYTES + 4;  // +length+crc

std::uint32_t crc32_simple(const unsigned char* data, std::size_t n) {
    // Очень простой полиномиальный хеш — не настоящий CRC32.
    // Этого достаточно для отлова случайных повреждений в учебной СУБД.
    std::uint32_t h = 0xCAFEBABEu;
    for (std::size_t i = 0; i < n; ++i) {
        h = h * 31u + data[i];
    }
    return h;
}

bool read_all(int fd, unsigned char* buf, std::size_t n) {
    std::size_t total = 0;
    while (total < n) {
        ssize_t r = ::read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;   // EOF
        total += static_cast<std::size_t>(r);
    }
    return true;
}

bool write_all(int fd, const unsigned char* buf, std::size_t n) {
    std::size_t total = 0;
    while (total < n) {
        ssize_t w = ::write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        total += static_cast<std::size_t>(w);
    }
    return true;
}

}  // namespace

WAL::WAL(const std::string& path) : path_(path), fd_(-1) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        throw WALError("не открыт WAL: " + path + ": " + std::strerror(errno));
    }
}

WAL::~WAL() {
    if (fd_ >= 0) ::close(fd_);
}

void WAL::log_insert(std::int64_t key, std::int64_t value) {
    unsigned char payload[PAYLOAD_BYTES];
    payload[0] = 1;   // тип = insert
    std::uint64_t uk = static_cast<std::uint64_t>(key);
    std::uint64_t uv = static_cast<std::uint64_t>(value);
    for (int i = 0; i < 8; ++i) payload[1 + i] = static_cast<unsigned char>((uk >> (i * 8)) & 0xFF);
    for (int i = 0; i < 8; ++i) payload[9 + i] = static_cast<unsigned char>((uv >> (i * 8)) & 0xFF);
    std::uint32_t crc = crc32_simple(payload, PAYLOAD_BYTES);

    unsigned char rec[RECORD_BYTES];
    std::uint32_t len = PAYLOAD_BYTES;
    for (int i = 0; i < 4; ++i) rec[i] = static_cast<unsigned char>((len >> (i * 8)) & 0xFF);
    std::memcpy(rec + 4, payload, PAYLOAD_BYTES);
    for (int i = 0; i < 4; ++i) rec[4 + PAYLOAD_BYTES + i] = static_cast<unsigned char>((crc >> (i * 8)) & 0xFF);

    if (!write_all(fd_, rec, RECORD_BYTES)) {
        throw WALError(std::string("WAL write: ") + std::strerror(errno));
    }
    if (::fsync(fd_) < 0) {
        throw WALError(std::string("WAL fsync: ") + std::strerror(errno));
    }
}

void WAL::replay(std::function<void(std::int64_t, std::int64_t)> cb) {
    // Открываем отдельный fd для чтения с offset 0.
    int rfd = ::open(path_.c_str(), O_RDONLY);
    if (rfd < 0) {
        throw WALError("WAL read open: " + path_);
    }

    unsigned char header[4];
    while (read_all(rfd, header, 4)) {
        std::uint32_t len = 0;
        for (int i = 0; i < 4; ++i) len |= static_cast<std::uint32_t>(header[i]) << (i * 8);
        if (len != PAYLOAD_BYTES) {
            // Битый заголовок — обрыв.
            break;
        }
        unsigned char body[PAYLOAD_BYTES];
        if (!read_all(rfd, body, PAYLOAD_BYTES)) break;
        unsigned char tail[4];
        if (!read_all(rfd, tail, 4)) break;
        std::uint32_t got_crc = 0;
        for (int i = 0; i < 4; ++i) got_crc |= static_cast<std::uint32_t>(tail[i]) << (i * 8);
        if (got_crc != crc32_simple(body, PAYLOAD_BYTES)) {
            // Повреждение — обрываем replay.
            break;
        }
        if (body[0] != 1) {
            break;  // неизвестный тип записи
        }
        std::uint64_t uk = 0, uv = 0;
        for (int i = 0; i < 8; ++i) uk |= static_cast<std::uint64_t>(body[1 + i]) << (i * 8);
        for (int i = 0; i < 8; ++i) uv |= static_cast<std::uint64_t>(body[9 + i]) << (i * 8);
        cb(static_cast<std::int64_t>(uk), static_cast<std::int64_t>(uv));
    }

    ::close(rfd);
}

void WAL::truncate() {
    if (::ftruncate(fd_, 0) < 0) {
        throw WALError(std::string("ftruncate: ") + std::strerror(errno));
    }
    // O_APPEND следит, чтобы новые записи писались с offset 0 после truncate.
    if (::fsync(fd_) < 0) {
        throw WALError(std::string("WAL truncate fsync: ") + std::strerror(errno));
    }
}

std::size_t WAL::size_on_disk() const {
    struct stat st;
    if (::fstat(fd_, &st) < 0) return 0;
    return static_cast<std::size_t>(st.st_size);
}

}  // namespace db
