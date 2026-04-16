#pragma once
#include <cstdint>

enum class PacketId : uint16_t {
    LoginRequest = 1,
    LoginResponse = 2,
    ChatMessage = 10,
    MoveRequest = 20,
    MoveResponse = 21,
};

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t size;
    PacketId packetId;
};

struct LoginRequestPacket {
    PacketHeader header;
    char username[32];
    char password[32];
};

struct LoginResponsePacket {
    PacketHeader header;
    bool success;
    uint64_t userId;
};

struct ChatMessagePacket {
    PacketHeader header;
    char message[256];
};

struct MoveRequestPacket {
    PacketHeader header;
    int32_t x;
    int32_t y;
};

struct MoveResponsePacket {
    PacketHeader header;
    uint64_t userId;
    int32_t x;
    int32_t y;
};

#pragma pack(pop)
