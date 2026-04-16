#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <vector>

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
        WSABUF wsaBuf;
    };

    struct SendOverlapped {
        OVERLAPPED overlapped;
        Session* owner;
        std::vector<BYTE> buffer;
    };

// WorkerThread에서 recvOverlapped_에 접근하므로 public으로 변경
public:
    SOCKET socket_;
    uint64_t sessionId_;
    std::atomic<SessionState> state_;
    std::atomic<int32_t> refCount_;

    RecvOverlapped recvOverlapped_;
    BYTE recvBuffer_[4096];

    std::atomic<int32_t> sendInProgress_;
    bool pendingDisconnect_;

    std::chrono::steady_clock::time_point lastRecvTime_;

public:
    Session();
    ~Session();

    void Reset(SOCKET socket, uint64_t sessionId);
    void Initialize();
    void Disconnect();

    void PostRecv();
    void PostSend(const BYTE* data, int32_t len);

    void ProcessRecv(DWORD bytesTransferred);
    void ProcessSend(SendOverlapped* sendOv, DWORD bytesTransferred);

    void AddRef();
    void Release();

    SOCKET GetSocket() const { return socket_; }
    uint64_t GetSessionId() const { return sessionId_; }
    SessionState GetState() const { return state_.load(); }

    void UpdateRecvTime();
    bool IsTimeout(int32_t timeoutSeconds) const;
};
