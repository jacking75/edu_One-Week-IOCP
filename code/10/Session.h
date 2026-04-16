#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <atomic>
#include <memory>
#include <vector>
#include "JobQueue.h"
#include "Protocol.h"
#include "LinearBuffer.h"

class Session : public std::enable_shared_from_this<Session> {
private:
    SOCKET socket_;
    uint64_t sessionId_;

    JobQueue* pJobQueue_;
    std::atomic<uint64_t> nextSequenceNumber_;

    // 수신 버퍼
    LinearBuffer recvBuffer_{16384};
    WSABUF recvWsaBuf_{};

    // 송신
    std::atomic<bool> isSending_{false};

    // 상태
    std::atomic<bool> isConnected_{true};

public:
    Session(SOCKET socket, uint64_t sessionId, JobQueue* pJobQueue)
        : socket_(socket)
        , sessionId_(sessionId)
        , pJobQueue_(pJobQueue)
        , nextSequenceNumber_(1) {}

    ~Session();

    // I/O 작업
    bool RegisterRecv();
    void OnRecvComplete(DWORD bytesTransferred);
    void ProcessReceivedData();

    // 패킷 처리 (로직 스레드에서 호출)
    void ProcessGameLogic(PacketId packetId, const char* pData, int size);

    // 전송
    void Send(const char* pData, int size);

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
