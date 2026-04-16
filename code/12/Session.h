#pragma once
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include "JobQueue.h"
#include "Protocol.h"
#include "LinearBuffer.h"

class Room;

class Session : public std::enable_shared_from_this<Session> {
public:
    using CreateRoomCallback = std::function<void(std::shared_ptr<Session>, const CreateRoomPacket*)>;
    using JoinRoomCallback = std::function<void(std::shared_ptr<Session>, const JoinRoomPacket*)>;
    using OmokPacketCallback = std::function<void(std::shared_ptr<Session>, PacketId, const char*, int)>;

private:
    SOCKET socket_;
    uint64_t sessionId_;
    JobQueue* pJobQueue_;
    std::atomic<uint64_t> nextSequenceNumber_;

    LinearBuffer recvBuffer_{16384};
    WSABUF recvWsaBuf_{};
    std::atomic<bool> isConnected_{true};

    std::shared_ptr<Room> pCurrentRoom_;
    std::mutex roomMutex_;

    CreateRoomCallback onCreateRoom_;
    JoinRoomCallback onJoinRoom_;
    OmokPacketCallback onOmokPacket_;

public:
    Session(SOCKET socket, uint64_t sessionId, JobQueue* pJobQueue)
        : socket_(socket), sessionId_(sessionId), pJobQueue_(pJobQueue)
        , nextSequenceNumber_(1) {}
    ~Session();

    bool RegisterRecv();
    void OnRecvComplete(DWORD bytesTransferred);
    void ProcessReceivedData();
    void ProcessGameLogic(PacketId packetId, const char* pData, int size);

    void Send(const char* pData, int size);
    void SendShared(std::shared_ptr<std::vector<char>> sharedBuffer);

    void SetCurrentRoom(std::shared_ptr<Room> pRoom);
    std::shared_ptr<Room> GetCurrentRoom();

    void SetCreateRoomCallback(CreateRoomCallback cb) { onCreateRoom_ = std::move(cb); }
    void SetJoinRoomCallback(JoinRoomCallback cb) { onJoinRoom_ = std::move(cb); }
    void SetOmokPacketCallback(OmokPacketCallback cb) { onOmokPacket_ = std::move(cb); }

    uint64_t GetSessionId() const { return sessionId_; }
    SOCKET GetSocket() const { return socket_; }
    bool IsConnected() const { return isConnected_.load(); }
    void Disconnect();

private:
    uint64_t GetNextSequenceNumber() {
        return nextSequenceNumber_.fetch_add(1, std::memory_order_relaxed);
    }
};

using SessionPtr = std::shared_ptr<Session>;
