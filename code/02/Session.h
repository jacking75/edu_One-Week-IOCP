#pragma once
#include "Common.h"

class Session : public std::enable_shared_from_this<Session> {
private:
    static std::atomic<SessionID> nextId_;

    SessionID id_;
    SOCKET socket_;
    IOContext recvContext_;
    IOContext sendContext_;
    std::atomic<bool> isConnected_;

public:
    Session(SOCKET socket)
        : id_(nextId_++), socket_(socket), isConnected_(true) {
        recvContext_.operation = IOOperation::RECV;
        sendContext_.operation = IOOperation::SEND;
    }

    ~Session() {
        Close();
    }

    SessionID GetId() const { return id_; }
    SOCKET GetSocket() const { return socket_; }
    bool IsConnected() const { return isConnected_; }
    IOContext* GetRecvContext() { return &recvContext_; }
    IOContext* GetSendContext() { return &sendContext_; }

    bool StartReceive() {
        recvContext_.Reset(IOOperation::RECV);

        DWORD flags = 0;
        DWORD recvBytes = 0;

        int result = WSARecv(
            socket_,
            &recvContext_.wsaBuf,
            1,
            &recvBytes,
            &flags,
            &recvContext_.overlapped,
            nullptr
        );

        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                LOG_ERROR("WSARecv failed: {}", error);
                return false;
            }
        }

        return true;
    }

    bool Send(const char* data, int length) {
        if (length > sizeof(sendContext_.buffer)) {
            LOG_ERROR("Send data too large");
            return false;
        }

        sendContext_.Reset(IOOperation::SEND);
        memcpy(sendContext_.buffer, data, length);
        sendContext_.wsaBuf.len = length;

        DWORD sendBytes = 0;
        int result = WSASend(
            socket_,
            &sendContext_.wsaBuf,
            1,
            &sendBytes,
            0,
            &sendContext_.overlapped,
            nullptr
        );

        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                LOG_ERROR("WSASend failed: {}", error);
                return false;
            }
        }

        return true;
    }

    void Close() {
        if (isConnected_.exchange(false)) {
            shutdown(socket_, SD_BOTH);
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            LOG_INFO("Session {} closed", id_);
        }
    }
};

std::atomic<SessionID> Session::nextId_(1);
