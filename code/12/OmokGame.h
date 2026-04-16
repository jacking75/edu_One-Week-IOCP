#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <chrono>
#include <string>

enum class OmokGameState {
    WaitingForPlayers,
    Ready,
    BlackTurn,
    WhiteTurn,
    Finished
};

struct WinCheckResult {
    bool isWin;
    uint8_t lineStartX;
    uint8_t lineStartY;
    uint8_t lineEndX;
    uint8_t lineEndY;
};

struct PlaceStoneResult {
    bool success;
    std::string errorMessage;
};

struct GameResultInfo {
    uint8_t result;     // OmokGameResult
    uint64_t winnerId;
    WinCheckResult winLine;
};

class OmokGame {
private:
    static constexpr int BOARD_SIZE = 15;

    OmokGameState state_;
    uint8_t board_[BOARD_SIZE][BOARD_SIZE];  // StoneColor values

    uint64_t blackPlayerId_;
    uint64_t whitePlayerId_;

    std::vector<std::pair<uint8_t, uint8_t>> moveHistory_;
    GameResultInfo gameResult_;

    uint32_t turnTimeLimit_;
    std::chrono::steady_clock::time_point turnStartTime_;

public:
    OmokGame();

    void SetPlayers(uint64_t blackId, uint64_t whiteId);
    void StartGame();

    PlaceStoneResult CanPlaceStone(uint64_t playerId, uint8_t x, uint8_t y) const;
    bool PlaceStone(uint64_t playerId, uint8_t x, uint8_t y, WinCheckResult& outWinResult);

    void OnPlayerSurrender(uint64_t playerId);
    void OnPlayerDisconnected(uint64_t playerId);

    OmokGameState GetState() const { return state_; }
    uint8_t GetStone(uint8_t x, uint8_t y) const { return board_[y][x]; }
    uint8_t GetCurrentTurnColor() const;
    uint64_t GetCurrentPlayerId() const;
    uint64_t GetBlackPlayerId() const { return blackPlayerId_; }
    uint64_t GetWhitePlayerId() const { return whitePlayerId_; }
    uint16_t GetMoveCount() const { return static_cast<uint16_t>(moveHistory_.size()); }
    const GameResultInfo& GetGameResult() const { return gameResult_; }

    bool IsTimeOut() const;
    uint32_t GetRemainingTimeMs() const;

private:
    bool CanTransitionTo(OmokGameState newState) const;
    void TransitionTo(OmokGameState newState);
    void OnStateEnter(OmokGameState newState);

    WinCheckResult CheckWin(uint8_t x, uint8_t y, uint8_t color) const;
    bool IsBoardFull() const;
};
