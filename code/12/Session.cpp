#include "Session.h"
#include "Room.h"
#include "IOContext.h"
#include <iostream>

Session::~Session() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool Session::RegisterRecv() {
    if (!IsConnected()) return false;
    recvBuffer_.Compact();

    auto ioContext = new IOContext();
    ioContext->ioType = IOType::Recv;
    ioContext->session = shared_from_this();

    recvWsaBuf_.buf = recvBuffer_.GetWriteBuffer();
    recvWsaBuf_.len = static_cast<ULONG>(recvBuffer_.GetFreeSize());

    DWORD flags = 0, bytes = 0;
    int result = WSARecv(socket_, &recvWsaBuf_, 1, &bytes, &flags,
                         &ioContext->overlapped, nullptr);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            delete ioContext;
            Disconnect();
            return false;
        }
    }
    return true;
}

void Session::OnRecvComplete(DWORD bytesTransferred) {
    if (bytesTransferred == 0) { Disconnect(); return; }
    recvBuffer_.MoveWritePos(bytesTransferred);
    ProcessReceivedData();
    RegisterRecv();
}

void Session::ProcessReceivedData() {
    constexpr size_t HEADER_SIZE = sizeof(PacketHeader);
    while (recvBuffer_.GetDataSize() >= HEADER_SIZE) {
        const char* bufferPtr = recvBuffer_.GetReadBuffer();
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(bufferPtr);
        uint16_t packetSize = header->size;

        if (packetSize < HEADER_SIZE || packetSize > 4096) {
            Disconnect(); return;
        }
        if (recvBuffer_.GetDataSize() < packetSize) break;

        auto packetId = header->packetId;
        uint64_t seq = GetNextSequenceNumber();
        std::vector<char> packetCopy(bufferPtr, bufferPtr + packetSize);
        auto self = shared_from_this();

        Job job([self, packetId, data = std::move(packetCopy)]() {
            self->ProcessGameLogic(packetId, data.data(), static_cast<int>(data.size()));
        }, sessionId_, seq);

        pJobQueue_->Enqueue(std::move(job));
        recvBuffer_.MoveReadPos(packetSize);
    }
}

void Session::ProcessGameLogic(PacketId packetId, const char* pData, int size) {
    switch (packetId) {
        case PacketId::CreateRoom: {
            if (size < static_cast<int>(sizeof(CreateRoomPacket))) break;
            if (onCreateRoom_) {
                onCreateRoom_(shared_from_this(),
                    reinterpret_cast<const CreateRoomPacket*>(pData));
            }
            break;
        }
        case PacketId::JoinRoom: {
            if (size < static_cast<int>(sizeof(JoinRoomPacket))) break;
            if (onJoinRoom_) {
                onJoinRoom_(shared_from_this(),
                    reinterpret_cast<const JoinRoomPacket*>(pData));
            }
            break;
        }
        case PacketId::LeaveRoom: {
            auto pRoom = GetCurrentRoom();
            if (pRoom) {
                pRoom->RemovePlayer(sessionId_);
                SetCurrentRoom(nullptr);
            }
            break;
        }
        case PacketId::GameStart: {
            auto pRoom = GetCurrentRoom();
            if (pRoom) pRoom->StartGame();
            break;
        }
        case PacketId::OmokPlaceStone:
        case PacketId::OmokSurrender: {
            if (onOmokPacket_) {
                onOmokPacket_(shared_from_this(), packetId, pData, size);
            }
            break;
        }
        default:
            break;
    }
}

void Session::Send(const char* pData, int size) {
    if (!IsConnected()) return;
    auto sendCtx = new SendContext();
    sendCtx->ioType = IOType::Send;
    sendCtx->session = shared_from_this();
    sendCtx->buffer = new char[size];
    sendCtx->size = size;
    memcpy(sendCtx->buffer, pData, size);

    WSABUF wsaBuf;
    wsaBuf.buf = sendCtx->buffer;
    wsaBuf.len = static_cast<ULONG>(size);

    DWORD bytesSent = 0;
    int result = WSASend(socket_, &wsaBuf, 1, &bytesSent, 0,
                         &sendCtx->overlapped, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        delete sendCtx;
        Disconnect();
    }
}

void Session::SendShared(std::shared_ptr<std::vector<char>> sharedBuffer) {
    if (!IsConnected()) return;
    auto sendCtx = new SendContext();
    sendCtx->ioType = IOType::Send;
    sendCtx->session = shared_from_this();
    sendCtx->sharedBuffer = sharedBuffer;
    sendCtx->buffer = nullptr;
    sendCtx->size = static_cast<int>(sharedBuffer->size());

    WSABUF wsaBuf;
    wsaBuf.buf = sharedBuffer->data();
    wsaBuf.len = static_cast<ULONG>(sharedBuffer->size());

    DWORD bytesSent = 0;
    int result = WSASend(socket_, &wsaBuf, 1, &bytesSent, 0,
                         &sendCtx->overlapped, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        delete sendCtx;
        Disconnect();
    }
}

void Session::SetCurrentRoom(std::shared_ptr<Room> pRoom) {
    std::lock_guard<std::mutex> lock(roomMutex_);
    pCurrentRoom_ = pRoom;
}

std::shared_ptr<Room> Session::GetCurrentRoom() {
    std::lock_guard<std::mutex> lock(roomMutex_);
    return pCurrentRoom_;
}

void Session::Disconnect() {
    bool expected = true;
    if (!isConnected_.compare_exchange_strong(expected, false)) return;

    std::cout << "[Session " << sessionId_ << "] Disconnected\n";

    auto pRoom = GetCurrentRoom();
    if (pRoom) {
        pRoom->RemovePlayer(sessionId_);
        SetCurrentRoom(nullptr);
    }

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}
