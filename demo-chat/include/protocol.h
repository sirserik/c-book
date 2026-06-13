#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

namespace chat {

// Типы фреймов. Конкретный список — это «версия 1» протокола.
enum MsgType : std::uint8_t {
    kHello   = 1,   // payload = nickname (utf-8)
    kMessage = 2,   // payload = текст сообщения
    kNotify  = 3,   // payload = серверное уведомление (вошёл/вышел и т.д.)
    kJoin    = 4,   // payload = имя комнаты
    kLeave   = 5,   // payload = имя комнаты
};

constexpr std::uint8_t PROTO_VERSION = 1;

// Формат фрейма:
//   [0..3]   u32 LE length (= 1 + 1 + payload.size(), т.е. size after length)
//   [4]      u8 version
//   [5]      u8 type
//   [6..]    payload (length-2 байт)

constexpr std::size_t HEADER_BYTES = 4 + 1 + 1;
constexpr std::uint32_t MAX_PAYLOAD = 64 * 1024;   // 64 KiB — здравый лимит

// Закодировать фрейм в байты. Бросает std::runtime_error при слишком большом
// payload.
std::vector<std::uint8_t> encode(std::uint8_t type, const std::string& payload);

// Попытаться извлечь один фрейм из накопителя.
// Возвращает:
//   true  — фрейм извлечён, заполнены out_type/out_payload, потреблены байты из acc.
//   false — данных пока недостаточно. Ничего не изменено.
// Бросает std::runtime_error на повреждённом фрейме (неверная версия, превышение размера).
bool try_decode(std::vector<std::uint8_t>& acc,
                std::uint8_t& out_type,
                std::string& out_payload);

}  // namespace chat

#endif
