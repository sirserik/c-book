#ifndef DB_BINARY_H
#define DB_BINARY_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace db {

// Запись/чтение чисел в little-endian с фиксированным размером.
// Не полагаемся на raw struct dump'ы — портабельность важнее микро-эффективности.

inline void write_u16(std::vector<unsigned char>& out, std::uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFFu));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFFu));
}

inline void write_u32(std::vector<unsigned char>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>((v >> (i * 8)) & 0xFFu));
    }
}

inline void write_u64(std::vector<unsigned char>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<unsigned char>((v >> (i * 8)) & 0xFFu));
    }
}

inline void write_string(std::vector<unsigned char>& out, const std::string& s) {
    // Длина (u32) + байты.
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

inline std::uint16_t read_u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

inline std::uint32_t read_u32(const unsigned char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (i * 8);
    }
    return v;
}

inline std::uint64_t read_u64(const unsigned char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return v;
}

inline std::string read_string(const unsigned char* p, std::size_t& consumed) {
    std::uint32_t len = read_u32(p);
    std::string s(reinterpret_cast<const char*>(p + 4), len);
    consumed = 4 + len;
    return s;
}

}  // namespace db

#endif
