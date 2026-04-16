#include "OmokRoom.h"
#include <iostream>
#include <cstring>

OmokRoom::OmokRoom(uint64_t id, const std::string& name,
                   const RoomConfig& config, int threadId)
    : Room(id, name, config, threadId)
    , pGame_(std::make_unique<OmokGame>()) {
}

void OmokRoom::StartGame() {
    Room::StartGame();

    auto playerList = GetPlayers();
    if (playerList.size() != 2) {
        std::cerr << "[OmokRoom " << roomId_ << "] Cannot start: need 2 players\n";
        return;
    }

    // First player is black, second is white
    pGame_->SetPlayers(playerList[0]->GetSessionId(),
                      playerList[1]->GetSessionId());
    pGame_->StartGame();

    std::cout << "[OmokRoom " << roomId_ << "] Omok game started. Black: "
              << playerList[0]->GetSessionId() << ", White: "
              << playerList[1]->GetSessionId() << "\n";

    // Broadcast initial game state
    OmokGameStatePacket packet{};
    packet.header.packetId = PacketId::OmokGameState;
    packet.header.size = sizeof(OmokGameStatePacket);

    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 15; ++x) {
            packet.board[y][x] = pGame_->GetStone(static_cast<uint8_t>(x),
                                                    static_cast<uint8_t>(y));
        }
    }

    packet.currentTurn = static_cast<StoneColor>(pGame_->GetCurrentTurnColor());
    packet.blackPlayerId = pGame_->GetBlackPlayerId();
    packet.whitePlayerId = pGame_->GetWhitePlayerId();
    packet.moveCount = pGame_->GetMoveCount();

    BroadcastToAll(reinterpret_cast<char*>(&packet), sizeof(packet));

    // Broadcast first turn
    BroadcastTurnChanged();
}

void OmokRoom::OnPlaceStone(uint64_t playerId, uint8_t x, uint8_t y) {
    // Check if placement is valid before placing
    auto canPlace = pGame_->CanPlaceStone(playerId, x, y);

    if (!canPlace.success) {
        // Send failure to the requesting player only
        OmokPlaceStoneResultPacket resultPacket{};
        resultPacket.header.packetId = PacketId::OmokPlaceStoneResult;
        resultPacket.header.size = sizeof(OmokPlaceStoneResultPacket);
        resultPacket.success = false;
        resultPacket.x = x;
        resultPacket.y = y;
        resultPacket.playerId = playerId;

        strncpy_s(resultPacket.errorMessage, canPlace.errorMessage.c_str(),
                  sizeof(resultPacket.errorMessage) - 1);

        auto pSession = FindPlayerSession(playerId);
        if (pSession) {
            pSession->Send(reinterpret_cast<char*>(&resultPacket), sizeof(resultPacket));
        }
        return;
    }

    // Get the stone color before placing (PlaceStone transitions the turn)
    uint8_t stoneColor = pGame_->GetCurrentTurnColor();

    WinCheckResult winResult;
    bool success = pGame_->PlaceStone(playerId, x, y, winResult);

    // Broadcast success result to all
    OmokPlaceStoneResultPacket resultPacket{};
    resultPacket.header.packetId = PacketId::OmokPlaceStoneResult;
    resultPacket.header.size = sizeof(OmokPlaceStoneResultPacket);
    resultPacket.success = success;
    resultPacket.x = x;
    resultPacket.y = y;
    resultPacket.stone = static_cast<StoneColor>(stoneColor);
    resultPacket.playerId = playerId;

    BroadcastToAll(reinterpret_cast<char*>(&resultPacket), sizeof(resultPacket));

    if (winResult.isWin || pGame_->GetState() == OmokGameState::Finished) {
        BroadcastGameEnd();
        EndGame();
    } else {
        BroadcastTurnChanged();
    }
}

void OmokRoom::OnSurrender(uint64_t playerId) {
    pGame_->OnPlayerSurrender(playerId);

    if (pGame_->GetState() == OmokGameState::Finished) {
        BroadcastGameEnd();
        EndGame();
    }
}

void OmokRoom::OnPlayerDisconnected(uint64_t playerId) {
    if (pGame_->GetState() == OmokGameState::BlackTurn ||
        pGame_->GetState() == OmokGameState::WhiteTurn) {
        pGame_->OnPlayerDisconnected(playerId);
        BroadcastGameEnd();
        EndGame();
    }
}

void OmokRoom::EndGame() {
    Room::EndGame();
}

bool OmokRoom::AddSpectator(std::shared_ptr<Session> pSession) {
    if (GetState() != RoomState::Playing) return false;

    uint64_t sessionId = pSession->GetSessionId();
    {
        std::lock_guard<std::mutex> lock(spectatorsMutex_);
        spectators_.insert(sessionId);
    }

    SendGameStateToSession(pSession);

    std::cout << "[OmokRoom " << roomId_ << "] Spectator " << sessionId
              << " joined\n";
    return true;
}

void OmokRoom::RemoveSpectator(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(spectatorsMutex_);
    spectators_.erase(sessionId);
}

int OmokRoom::GetSpectatorCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(spectatorsMutex_));
    return static_cast<int>(spectators_.size());
}

void OmokRoom::BroadcastToAll(const char* pData, int size) {
    // Players
    BroadcastPacket(pData, size);

    // Spectators
    std::lock_guard<std::mutex> lock(spectatorsMutex_);
    for (uint64_t specId : spectators_) {
        auto pSession = FindPlayerSession(specId);
        if (pSession) {
            pSession->Send(pData, size);
        }
    }
}

void OmokRoom::SendGameStateToSession(std::shared_ptr<Session> pSession) {
    OmokGameStatePacket packet{};
    packet.header.packetId = PacketId::OmokGameState;
    packet.header.size = sizeof(OmokGameStatePacket);

    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 15; ++x) {
            packet.board[y][x] = pGame_->GetStone(static_cast<uint8_t>(x),
                                                    static_cast<uint8_t>(y));
        }
    }

    packet.currentTurn = static_cast<StoneColor>(pGame_->GetCurrentTurnColor());
    packet.blackPlayerId = pGame_->GetBlackPlayerId();
    packet.whitePlayerId = pGame_->GetWhitePlayerId();
    packet.moveCount = pGame_->GetMoveCount();

    pSession->Send(reinterpret_cast<char*>(&packet), sizeof(packet));
}

void OmokRoom::BroadcastTurnChanged() {
    OmokTurnChangedPacket packet{};
    packet.header.packetId = PacketId::OmokTurnChanged;
    packet.header.size = sizeof(OmokTurnChangedPacket);
    packet.currentTurn = static_cast<StoneColor>(pGame_->GetCurrentTurnColor());
    packet.currentPlayerId = pGame_->GetCurrentPlayerId();
    packet.remainingTimeMs = pGame_->GetRemainingTimeMs();

    BroadcastToAll(reinterpret_cast<char*>(&packet), sizeof(packet));
}

void OmokRoom::BroadcastGameEnd() {
    const auto& result = pGame_->GetGameResult();

    OmokGameEndPacket packet{};
    packet.header.packetId = PacketId::OmokGameEnd;
    packet.header.size = sizeof(OmokGameEndPacket);
    packet.result = static_cast<OmokGameResult>(result.result);
    packet.winnerId = result.winnerId;
    packet.winLineStart[0] = result.winLine.lineStartX;
    packet.winLineStart[1] = result.winLine.lineStartY;
    packet.winLineEnd[0] = result.winLine.lineEndX;
    packet.winLineEnd[1] = result.winLine.lineEndY;

    BroadcastToAll(reinterpret_cast<char*>(&packet), sizeof(packet));
}
