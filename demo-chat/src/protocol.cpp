#include "protocol.h"

#include <stdexcept>
#include <string>

namespace chat {

namespace {

void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (i * 8);
    return v;
}

}  // namespace

std::vector<std::uint8_t> encode(std::uint8_t type, const std::string& payload) {
    if (payload.size() > MAX_PAYLOAD) {
        throw std::runtime_error("payload too large");
    }
    std::vector<std::uint8_t> out;
    // length = version(1) + type(1) + payload(N)
    std::uint32_t len = static_cast<std::uint32_t>(2 + payload.size());
    out.reserve(4 + len);
    write_u32_le(out, len);
    out.push_back(PROTO_VERSION);
    out.push_back(type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool try_decode(std::vector<std::uint8_t>& acc,
                std::uint8_t& out_type,
                std::string& out_payload) {
    if (acc.size() < 4) return false;
    std::uint32_t len = read_u32_le(acc.data());
    if (len < 2 || len > MAX_PAYLOAD + 2) {
        throw std::runtime_error("invalid frame length: " + std::to_string(len));
    }
    if (acc.size() < 4 + len) return false;

    std::uint8_t version = acc[4];
    if (version != PROTO_VERSION) {
        throw std::runtime_error("unsupported version: " + std::to_string(version));
    }
    out_type = acc[5];
    out_payload.assign(reinterpret_cast<const char*>(acc.data() + 6), len - 2);

    acc.erase(acc.begin(), acc.begin() + 4 + len);
    return true;
}

}  // namespace chat
