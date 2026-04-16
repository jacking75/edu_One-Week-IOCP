
#pragma once

#include <cstdint>

constexpr int MAX_PACKET_SIZE = 4096;

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t size;
    uint16_t packetId;
};
#pragma pack(pop)

enum class PacketID : uint16_t {
    NONE = 0,
    ECHO = 1,
};
