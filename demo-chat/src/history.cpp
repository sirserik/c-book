#include "history.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace chat {

namespace {

void w_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void w_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}
std::uint16_t r_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t r_u32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (i * 8);
    return v;
}

bool read_all(int fd, std::uint8_t* buf, std::size_t n) {
    std::size_t total = 0;
    while (total < n) {
        ssize_t r = ::read(fd, buf + total, n - total);
        if (r <= 0) return false;
        total += static_cast<std::size_t>(r);
    }
    return true;
}

}  // namespace

History::History(const std::string& path) : path_(path), fd_(-1) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) throw std::runtime_error("history open: " + path);
}

History::~History() {
    if (fd_ >= 0) ::close(fd_);
}

void History::append(const HistoryRecord& r) {
    std::vector<std::uint8_t> body;
    w_u32(body, r.timestamp);
    w_u16(body, static_cast<std::uint16_t>(r.name.size()));
    w_u16(body, static_cast<std::uint16_t>(r.room.size()));
    w_u16(body, static_cast<std::uint16_t>(r.text.size()));
    body.insert(body.end(), r.name.begin(), r.name.end());
    body.insert(body.end(), r.room.begin(), r.room.end());
    body.insert(body.end(), r.text.begin(), r.text.end());

    std::vector<std::uint8_t> framed;
    w_u32(framed, static_cast<std::uint32_t>(body.size()));
    framed.insert(framed.end(), body.begin(), body.end());

    std::size_t off = 0;
    while (off < framed.size()) {
        ssize_t w = ::write(fd_, framed.data() + off, framed.size() - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("history write");
        }
        off += static_cast<std::size_t>(w);
    }
    ::fsync(fd_);
}

std::vector<HistoryRecord> History::recent(const std::string& room, std::size_t n) {
    std::vector<HistoryRecord> all;
    int rfd = ::open(path_.c_str(), O_RDONLY);
    if (rfd < 0) return all;

    std::uint8_t hdr[4];
    while (read_all(rfd, hdr, 4)) {
        std::uint32_t body_len = r_u32(hdr);
        std::vector<std::uint8_t> body(body_len);
        if (!read_all(rfd, body.data(), body_len)) break;

        HistoryRecord rec;
        rec.timestamp = r_u32(body.data());
        std::uint16_t nl = r_u16(body.data() + 4);
        std::uint16_t rl = r_u16(body.data() + 6);
        std::uint16_t tl = r_u16(body.data() + 8);
        std::size_t pos = 10;
        rec.name.assign(reinterpret_cast<const char*>(body.data() + pos), nl); pos += nl;
        rec.room.assign(reinterpret_cast<const char*>(body.data() + pos), rl); pos += rl;
        rec.text.assign(reinterpret_cast<const char*>(body.data() + pos), tl);

        if (room.empty() || rec.room == room) {
            all.push_back(std::move(rec));
        }
    }
    ::close(rfd);

    if (all.size() > n) {
        all.erase(all.begin(), all.end() - static_cast<std::ptrdiff_t>(n));
    }
    return all;
}

}  // namespace chat
