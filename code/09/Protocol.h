
#pragma once

#include <cstdint>

enum class HeartbeatPacketId : uint16_t {
    Ping = 100,
    Pong = 101,
};

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t size;
    uint16_t packetId;
};

struct PingPacket {
    PacketHeader header;
    uint64_t timestamp;
};

struct PongPacket {
    PacketHeader header;
    uint64_t timestamp;
};

#pragma pack(pop)
