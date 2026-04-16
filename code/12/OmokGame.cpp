#include "OmokGame.h"
#include "Protocol.h"
#include <iostream>

OmokGame::OmokGame()
    : state_(OmokGameState::WaitingForPlayers)
    , blackPlayerId_(0)
    , whitePlayerId_(0)
    , turnTimeLimit_(30) {

    for (int y = 0; y < BOARD_SIZE; ++y) {
        for (int x = 0; x < BOARD_SIZE; ++x) {
            board_[y][x] = static_cast<uint8_t>(StoneColor::None);
        }
    }

    gameResult_ = {};
}

void OmokGame::SetPlayers(uint64_t blackId, uint64_t whiteId) {
    blackPlayerId_ = blackId;
    whitePlayerId_ = whiteId;

    if (blackPlayerId_ != 0 && whitePlayerId_ != 0) {
        TransitionTo(OmokGameState::Ready);
    }
}

void OmokGame::StartGame() {
    if (state_ != OmokGameState::Ready) {
        return;
    }

    TransitionTo(OmokGameState::BlackTurn);
}

PlaceStoneResult OmokGame::CanPlaceStone(uint64_t playerId, uint8_t x, uint8_t y) const {
    PlaceStoneResult result;
    result.success = false;

    if (state_ != OmokGameState::BlackTurn && state_ != OmokGameState::WhiteTurn) {
        result.errorMessage = "Game is not in progress";
        return result;
    }

    uint64_t currentPlayerId = GetCurrentPlayerId();
    if (playerId != currentPlayerId) {
        result.errorMessage = "Not your turn";
        return result;
    }

    if (x >= BOARD_SIZE || y >= BOARD_SIZE) {
        result.errorMessage = "Invalid coordinates";
        return result;
    }

    if (board_[y][x] != static_cast<uint8_t>(StoneColor::None)) {
        result.errorMessage = "Stone already exists";
        return result;
    }

    result.success = true;
    return result;
}

bool OmokGame::PlaceStone(uint64_t playerId, uint8_t x, uint8_t y,
                         WinCheckResult& outWinResult) {
    auto canPlace = CanPlaceStone(playerId, x, y);
    if (!canPlace.success) {
        std::cerr << "[OmokGame] PlaceStone failed: "
                  << canPlace.errorMessage << "\n";
        return false;
    }

    uint8_t color = GetCurrentTurnColor();
    board_[y][x] = color;
    moveHistory_.push_back({x, y});

    std::cout << "[OmokGame] Stone placed at (" << (int)x << ", " << (int)y
              << ") by player " << playerId << "\n";

    outWinResult = CheckWin(x, y, color);

    if (outWinResult.isWin) {
        gameResult_.result = (color == static_cast<uint8_t>(StoneColor::Black))
            ? static_cast<uint8_t>(OmokGameResult::BlackWin)
            : static_cast<uint8_t>(OmokGameResult::WhiteWin);
        gameResult_.winnerId = playerId;
        gameResult_.winLine = outWinResult;

        TransitionTo(OmokGameState::Finished);

        std::cout << "[OmokGame] Game finished! Winner: " << playerId << "\n";
    } else if (IsBoardFull()) {
        gameResult_.result = static_cast<uint8_t>(OmokGameResult::Draw);
        gameResult_.winnerId = 0;

        TransitionTo(OmokGameState::Finished);

        std::cout << "[OmokGame] Game finished! Draw\n";
    } else {
        OmokGameState nextState = (state_ == OmokGameState::BlackTurn)
            ? OmokGameState::WhiteTurn : OmokGameState::BlackTurn;
        TransitionTo(nextState);
    }

    return true;
}

void OmokGame::OnPlayerSurrender(uint64_t playerId) {
    if (state_ != OmokGameState::BlackTurn && state_ != OmokGameState::WhiteTurn) {
        return;
    }

    uint64_t winnerId = (playerId == blackPlayerId_)
        ? whitePlayerId_ : blackPlayerId_;

    gameResult_.result = static_cast<uint8_t>(OmokGameResult::Surrender);
    gameResult_.winnerId = winnerId;

    TransitionTo(OmokGameState::Finished);

    std::cout << "[OmokGame] Player " << playerId
              << " surrendered. Winner: " << winnerId << "\n";
}

void OmokGame::OnPlayerDisconnected(uint64_t playerId) {
    OnPlayerSurrender(playerId);
}

uint8_t OmokGame::GetCurrentTurnColor() const {
    return (state_ == OmokGameState::BlackTurn)
        ? static_cast<uint8_t>(StoneColor::Black)
        : static_cast<uint8_t>(StoneColor::White);
}

uint64_t OmokGame::GetCurrentPlayerId() const {
    return (state_ == OmokGameState::BlackTurn)
        ? blackPlayerId_ : whitePlayerId_;
}

bool OmokGame::IsTimeOut() const {
    if (state_ != OmokGameState::BlackTurn && state_ != OmokGameState::WhiteTurn) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - turnStartTime_).count();

    return elapsed >= turnTimeLimit_;
}

uint32_t OmokGame::GetRemainingTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - turnStartTime_).count();

    int32_t remaining = (turnTimeLimit_ * 1000) - static_cast<int32_t>(elapsed);
    return (remaining > 0) ? remaining : 0;
}

bool OmokGame::CanTransitionTo(OmokGameState newState) const {
    switch (state_) {
        case OmokGameState::WaitingForPlayers:
            return newState == OmokGameState::Ready;
        case OmokGameState::Ready:
            return newState == OmokGameState::BlackTurn;
        case OmokGameState::BlackTurn:
            return newState == OmokGameState::WhiteTurn ||
                   newState == OmokGameState::Finished;
        case OmokGameState::WhiteTurn:
            return newState == OmokGameState::BlackTurn ||
                   newState == OmokGameState::Finished;
        case OmokGameState::Finished:
            return false;
        default:
            return false;
    }
}

void OmokGame::TransitionTo(OmokGameState newState) {
    if (!CanTransitionTo(newState)) {
        std::cerr << "[OmokGame] Invalid state transition: "
                  << static_cast<int>(state_) << " -> "
                  << static_cast<int>(newState) << "\n";
        return;
    }

    state_ = newState;
    OnStateEnter(newState);
}

void OmokGame::OnStateEnter(OmokGameState newState) {
    if (newState == OmokGameState::BlackTurn ||
        newState == OmokGameState::WhiteTurn) {
        turnStartTime_ = std::chrono::steady_clock::now();
    }
}

WinCheckResult OmokGame::CheckWin(uint8_t x, uint8_t y, uint8_t color) const {
    WinCheckResult result{false, 0, 0, 0, 0};

    int dx[] = {1, 0, 1, 1};
    int dy[] = {0, 1, 1, -1};

    for (int dir = 0; dir < 4; ++dir) {
        int count = 1;
        int startX = x, startY = y;
        int endX = x, endY = y;

        // Forward
        for (int step = 1; step < 5; ++step) {
            int nx = x + dx[dir] * step;
            int ny = y + dy[dir] * step;

            if (nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) break;
            if (board_[ny][nx] != color) break;

            count++;
            endX = nx;
            endY = ny;
        }

        // Backward
        for (int step = 1; step < 5; ++step) {
            int nx = x - dx[dir] * step;
            int ny = y - dy[dir] * step;

            if (nx < 0 || nx >= BOARD_SIZE || ny < 0 || ny >= BOARD_SIZE) break;
            if (board_[ny][nx] != color) break;

            count++;
            startX = nx;
            startY = ny;
        }

        if (count >= 5) {
            result.isWin = true;
            result.lineStartX = static_cast<uint8_t>(startX);
            result.lineStartY = static_cast<uint8_t>(startY);
            result.lineEndX = static_cast<uint8_t>(endX);
            result.lineEndY = static_cast<uint8_t>(endY);
            return result;
        }
    }

    return result;
}

bool OmokGame::IsBoardFull() const {
    return moveHistory_.size() >= (BOARD_SIZE * BOARD_SIZE);
}
