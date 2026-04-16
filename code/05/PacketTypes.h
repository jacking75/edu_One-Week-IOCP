
#pragma once

#include <cstdint>

constexpr uint16_t MIN_PACKET_SIZE = 8;
constexpr uint16_t MAX_PACKET_SIZE = 8192;

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t size;
    uint16_t packetId;
    uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 8, "PacketHeader must be 8 bytes");

enum class PacketID : uint16_t {
    NONE = 0,

    HEARTBEAT = 1,

    LOGIN_REQ = 1000,
    LOGIN_RES = 1001,

    CHAT_REQ = 2000,
    CHAT_BROADCAST = 2001,

    ECHO_REQ = 9000,
    ECHO_RES = 9001,
};

inline bool IsValidPacketSize(uint16_t size) {
    return size >= MIN_PACKET_SIZE && size <= MAX_PACKET_SIZE;
}

inline bool IsValidPacketID(uint16_t packetId) {
    return packetId > 0 && packetId < 10000;
}
