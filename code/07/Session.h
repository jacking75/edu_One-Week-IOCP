
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <memory>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <concurrent_queue.h>
#include "RingBuffer.h"
#include "Packet.h"

enum class SessionState : int32_t
{
    CONNECTING = 0,
    CONNECTED = 1,
    DISCONNECTING = 2,
    DISCONNECTED = 3
};

enum class IOType
{
    RECV,
    SEND
};

struct IOContext
{
    OVERLAPPED overlapped;
    std::shared_ptr<class Session> session;
    IOType ioType;

    IOContext()
    {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    using Job = std::function<void()>;

    explicit Session(SOCKET socket);
    ~Session();

    // 기본 정보
    uint32_t GetID() const { return sessionID_; }
    SOCKET GetSocket() const { return socket_; }

    // 상태 관리 (원자적)
    SessionState GetState() const
    {
        return state_.load(std::memory_order_acquire);
    }

    bool IsConnected() const
    {
        return GetState() == SessionState::CONNECTED;
    }

    bool TryDisconnect()
    {
        SessionState expected = SessionState::CONNECTED;
        return state_.compare_exchange_strong(
            expected,
            SessionState::DISCONNECTING,
            std::memory_order_acq_rel
        );
    }

    // I/O 작업
    bool RegisterRecv();
    bool RegisterSend();
    void Send(PacketPtr packet);

    // I/O 완료 처리
    void OnRecv(DWORD bytesTransferred);
    void OnSend(DWORD bytesTransferred);

    // 인증 관련
    bool IsAuthenticated() const { return isAuthenticated_.load(); }
    void SetAuthenticated(bool value) { isAuthenticated_.store(value); }

    const std::string& GetUserID() const { return userID_; }
    void SetUserID(const std::string& userID) { userID_ = userID; }

    // 위치 관련 (원자적)
    void SetPosition(int32_t x, int32_t y)
    {
        posX_.store(x, std::memory_order_relaxed);
        posY_.store(y, std::memory_order_relaxed);
    }

    void GetPosition(int32_t& x, int32_t& y) const
    {
        x = posX_.load(std::memory_order_relaxed);
        y = posY_.load(std::memory_order_relaxed);
    }

    // 초기화 (IOCP 등록 + 수신 시작)
    void Initialize();

    // 연결 종료 (public)
    void Disconnect();

private:
    // 작업 직렬화
    void EnqueueJob(Job job);
    void TryProcessJobs();
    bool TryBeginProcess();
    void EndProcess();

    // 패킷 처리
    void ProcessPackets();
    std::vector<PacketPtr> ExtractPackets();

    // 연결 종료 완료
    void OnDisconnected();

private:
    // 기본 정보
    static std::atomic<uint32_t> s_nextSessionID;
    uint32_t sessionID_;
    SOCKET socket_;

    // 상태
    std::atomic<SessionState> state_{SessionState::CONNECTING};
    std::atomic<bool> isAuthenticated_{false};
    std::string userID_;

    // 위치
    std::atomic<int32_t> posX_{0};
    std::atomic<int32_t> posY_{0};

    // 버퍼
    RingBuffer recvBuffer_{8192};
    RingBuffer sendBuffer_{8192};

    // 송신 큐 (thread-safe)
    concurrency::concurrent_queue<PacketPtr> sendQueue_;
    std::atomic<bool> isSending_{false};

    // 작업 직렬화
    std::atomic<int32_t> isProcessing_{0};
    concurrency::concurrent_queue<Job> jobQueue_;

    // WSABUF
    WSABUF recvWsaBuf_{};
    WSABUF sendWsaBuf_{};
};

using SessionPtr = std::shared_ptr<Session>;
