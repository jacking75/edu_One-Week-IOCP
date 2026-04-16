#pragma once
#include <cstdint>

enum class PacketId : uint16_t {
    // Room management
    CreateRoom = 30,
    CreateRoomResponse = 31,
    JoinRoom = 32,
    JoinRoomResponse = 33,
    LeaveRoom = 34,
    RoomList = 35,
    RoomState = 36,

    PlayerJoined = 40,
    PlayerLeft = 41,
    PlayerKicked = 42,

    GameStart = 50,
    GameEnd = 51,
};

enum class RoomState : uint8_t {
    Waiting,
    Playing,
    Finished
};

enum class KickReason : uint8_t {
    RoomClosed = 0,
    HostKicked = 1,
    Timeout = 2,
};

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t size;
    PacketId packetId;
};

struct CreateRoomPacket {
    PacketHeader header;
    char roomName[32];
    uint32_t maxPlayers;
    uint32_t minPlayers;
    bool isPrivate;
    char password[16];
};

struct CreateRoomResponsePacket {
    PacketHeader header;
    bool success;
    uint64_t roomId;
};

struct JoinRoomPacket {
    PacketHeader header;
    uint64_t roomId;
    char password[16];
};

struct JoinRoomResponsePacket {
    PacketHeader header;
    bool success;
    uint64_t roomId;
};

struct RoomStatePacket {
    PacketHeader header;
    uint64_t roomId;
    RoomState state;
    uint32_t playerCount;
    uint32_t maxPlayers;
    uint64_t hostSessionId;
    uint64_t playerIds[16];
};

struct PlayerJoinedPacket {
    PacketHeader header;
    uint64_t roomId;
    uint64_t sessionId;
    uint32_t currentPlayerCount;
};

struct PlayerLeftPacket {
    PacketHeader header;
    uint64_t roomId;
    uint64_t sessionId;
    uint32_t currentPlayerCount;
};

struct PlayerKickedPacket {
    PacketHeader header;
    KickReason reason;
};

struct GameStartPacket {
    PacketHeader header;
    uint64_t roomId;
    uint32_t playerCount;
};

struct GameEndPacket {
    PacketHeader header;
    uint64_t roomId;
    uint64_t winnerSessionId;
};

#pragma pack(pop)
