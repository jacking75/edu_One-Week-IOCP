
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <memory>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <chrono>
#include <concurrent_queue.h>
#include "LinearBuffer.h"
#include "Packet.h"

enum class SessionState : int32_t
{
    CONNECTING = 0,
    CONNECTED = 1,
    DISCONNECTING = 2,
    DISCONNECTED = 3
};

enum class DisconnectReason
{
    Normal,
    Timeout,
    Error
};

class TimerManager;

class Session : public std::enable_shared_from_this<Session>
{
public:
    using Job = std::function<void()>;

    explicit Session(SOCKET socket, TimerManager* pTimerManager);
    ~Session();

    uint32_t GetID() const { return sessionID_; }
    SOCKET GetSocket() const { return socket_; }

    SessionState GetState() const {
        return state_.load(std::memory_order_acquire);
    }

    bool IsConnected() const {
        return GetState() == SessionState::CONNECTED;
    }

    bool TryDisconnect();

    // I/O 작업
    bool RegisterRecv();
    bool RegisterSend();
    void Send(PacketPtr packet);

    // I/O 완료 처리
    void OnRecv(DWORD bytesTransferred);
    void OnSend(struct SendContext* sendContext, DWORD bytesTransferred);

    // 초기화
    void Initialize();

    // 연결 종료
    void Disconnect();
    void ForceDisconnect(DisconnectReason reason);

    // 인증
    bool IsAuthenticated() const { return isAuthenticated_.load(); }
    void SetAuthenticated(bool value) { isAuthenticated_.store(value); }
    const std::string& GetUserID() const { return userID_; }
    void SetUserID(const std::string& userID) { userID_ = userID; }

    // 위치
    void SetPosition(int32_t x, int32_t y) {
        posX_.store(x, std::memory_order_relaxed);
        posY_.store(y, std::memory_order_relaxed);
    }
    void GetPosition(int32_t& x, int32_t& y) const {
        x = posX_.load(std::memory_order_relaxed);
        y = posY_.load(std::memory_order_relaxed);
    }

    // 하트비트
    void StartHeartbeat();
    void StopHeartbeat();
    void SendPing();
    void OnPongReceived(uint64_t timestamp);
    void CheckHeartbeatTimeout();
    void UpdateLastRecvTime();

private:
    void ProcessReceivedData();
    void OnDisconnected();

    void EnqueueJob(Job job);
    void TryProcessJobs();
    bool TryBeginProcess();
    void EndProcess();

private:
    static std::atomic<uint32_t> s_nextSessionID;
    uint32_t sessionID_;
    SOCKET socket_;

    std::atomic<SessionState> state_{SessionState::CONNECTING};
    std::atomic<bool> isAuthenticated_{false};
    std::string userID_;

    std::atomic<int32_t> posX_{0};
    std::atomic<int32_t> posY_{0};

    // Zero-Copy 수신 버퍼
    LinearBuffer recvBuffer_{16384};
    WSABUF recvWsaBuf_{};

    // Scatter-Gather 송신 큐
    concurrency::concurrent_queue<PacketPtr> sendQueue_;
    std::atomic<bool> isSending_{false};

    // 작업 직렬화
    std::atomic<int32_t> isProcessing_{0};
    concurrency::concurrent_queue<Job> jobQueue_;

    // 하트비트
    TimerManager* pTimerManager_;
    std::atomic<std::chrono::steady_clock::time_point> lastRecvTime_;
    std::chrono::seconds heartbeatTimeout_{30};
    uint64_t heartbeatTimerId_{0};
    uint64_t timeoutCheckTimerId_{0};
};

using SessionPtr = std::shared_ptr<Session>;
