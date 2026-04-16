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

    // Omok game
    OmokPlaceStone = 100,
    OmokPlaceStoneResult = 101,
    OmokGameState = 102,
    OmokTurnChanged = 103,
    OmokGameEnd = 104,
    OmokSurrender = 105,
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

enum class StoneColor : uint8_t {
    None = 0,
    Black = 1,
    White = 2
};

enum class OmokGameResult : uint8_t {
    BlackWin = 0,
    WhiteWin = 1,
    Draw = 2,
    Surrender = 3
};

#pragma pack(push, 1)

struct PacketHeader {
    uint16_t size;
    PacketId packetId;
};

// Room packets
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

// Omok packets
struct OmokPlaceStonePacket {
    PacketHeader header;
    uint8_t x;
    uint8_t y;
};

struct OmokPlaceStoneResultPacket {
    PacketHeader header;
    bool success;
    uint8_t x;
    uint8_t y;
    StoneColor stone;
    uint64_t playerId;
    char errorMessage[64];
};

struct OmokGameStatePacket {
    PacketHeader header;
    uint8_t board[15][15];
    StoneColor currentTurn;
    uint64_t blackPlayerId;
    uint64_t whitePlayerId;
    uint16_t moveCount;
};

struct OmokTurnChangedPacket {
    PacketHeader header;
    StoneColor currentTurn;
    uint64_t currentPlayerId;
    uint32_t remainingTimeMs;
};

struct OmokGameEndPacket {
    PacketHeader header;
    OmokGameResult result;
    uint64_t winnerId;
    uint8_t winLineStart[2];
    uint8_t winLineEnd[2];
};

struct OmokSurrenderPacket {
    PacketHeader header;
};

#pragma pack(pop)
