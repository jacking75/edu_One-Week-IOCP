
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "PacketHeader.h"
#include "BufferConfig.h"

enum class SessionState {
    Idle,
    Connected,
    Disconnecting,
    Disconnected
};

class Session {
public:
    struct RecvOverlapped {
        OVERLAPPED overlapped;
        Session* owner;
    };

    struct SendOverlapped {
        OVERLAPPED overlapped;
        Session* owner;
    };

    // WorkerThread에서 접근
    RecvOverlapped recvOverlapped_;

private:
    SOCKET socket_;
    uint64_t sessionId_;
    std::atomic<SessionState> state_;
    std::atomic<int32_t> refCount_;

    RecvBuffer recvBuffer_;

    SendBuffer sendBuffer_;
    std::atomic<bool> sendRegistered_;

    std::chrono::steady_clock::time_point lastRecvTime_;

public:
    Session();
    ~Session();

    void Reset(SOCKET socket, uint64_t sessionId);
    void Initialize();
    void Disconnect();

    void PostRecv();
    void Send(const BYTE* data, int size);

    void ProcessRecv(DWORD bytesTransferred);
    void ProcessSend(SendOverlapped* sendOv, DWORD bytesTransferred);

    void AddRef();
    void Release();

    SOCKET GetSocket() const { return socket_; }
    uint64_t GetSessionId() const { return sessionId_; }
    SessionState GetState() const { return state_.load(); }

    void UpdateRecvTime();
    bool IsTimeout(int32_t timeoutSeconds) const;

private:
    void RegisterSend();
    void OnPacket(const PacketHeader& header);
};
