
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <memory>
#include <string>
#include <atomic>
#include <vector>
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "BufferConfig.h"
#include "Packet.h"

class Session;
using SessionPtr = std::shared_ptr<Session>;

enum class SessionState {
    Idle,
    Connected,
    Disconnecting,
    Disconnected
};

class Session : public std::enable_shared_from_this<Session> {
public:
    struct RecvOverlapped {
        OVERLAPPED overlapped;
        Session* owner;
    };

    struct SendOverlapped {
        OVERLAPPED overlapped;
        Session* owner;
    };

    RecvOverlapped recvOverlapped_;

    explicit Session(SOCKET socket, uint64_t sessionId);
    ~Session();

    void Initialize();
    void Disconnect();
    void PostRecv();
    void Send(PacketPtr packet);

    void ProcessRecv(DWORD bytesTransferred);
    void ProcessSend(SendOverlapped* sendOv, DWORD bytesTransferred);

    void AddRef();
    void Release();

    uint64_t GetID() const { return sessionId_; }
    SOCKET GetSocket() const { return socket_; }
    SessionState GetState() const { return state_.load(); }

    // 인증 관련
    bool IsAuthenticated() const { return isAuthenticated_.load(); }
    void SetAuthenticated(bool value) { isAuthenticated_.store(value); }

    const std::string& GetUserID() const { return userID_; }
    void SetUserID(const std::string& userID) { userID_ = userID; }

    // 위치 관련
    void SetPosition(int32_t x, int32_t y) {
        posX_.store(x);
        posY_.store(y);
    }

    void GetPosition(int32_t& x, int32_t& y) const {
        x = posX_.load();
        y = posY_.load();
    }

    // 패킷 추출
    std::vector<PacketPtr> ExtractPackets();

private:
    void RegisterSend();

    SOCKET socket_;
    uint64_t sessionId_;
    std::atomic<SessionState> state_;
    std::atomic<int32_t> refCount_;

    RecvBuffer recvBuffer_;
    SendBuffer sendBuffer_;
    std::atomic<bool> sendRegistered_;

    std::atomic<bool> isAuthenticated_{false};
    std::string userID_;
    std::atomic<int32_t> posX_{0};
    std::atomic<int32_t> posY_{0};
};
