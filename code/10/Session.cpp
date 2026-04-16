#include "Session.h"
#include "IOContext.h"
#include <iostream>
#include <vector>

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

    DWORD flags = 0;
    DWORD bytes = 0;

    int result = WSARecv(socket_, &recvWsaBuf_, 1, &bytes, &flags,
                         &ioContext->overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cerr << "[Session " << sessionId_
                      << "] WSARecv failed: " << error << "\n";
            delete ioContext;
            Disconnect();
            return false;
        }
    }

    return true;
}

void Session::OnRecvComplete(DWORD bytesTransferred) {
    if (bytesTransferred == 0) {
        Disconnect();
        return;
    }

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
            std::cerr << "[Session " << sessionId_
                      << "] Invalid packet size: " << packetSize << "\n";
            Disconnect();
            return;
        }

        if (recvBuffer_.GetDataSize() < packetSize) {
            break;  // 아직 패킷이 완전히 도착하지 않음
        }

        // I/O 스레드에서 호출됨 - Job으로 변환하여 로직 큐에 추가
        auto packetId = header->packetId;
        uint64_t seq = GetNextSequenceNumber();

        // 패킷 데이터 복사 (수신 버퍼 재사용을 위해)
        std::vector<char> packetCopy(bufferPtr, bufferPtr + packetSize);

        // shared_ptr로 안전한 참조
        auto self = shared_from_this();

        Job job([self, packetId, data = std::move(packetCopy)]() {
            // 로직 스레드에서 실행됨
            self->ProcessGameLogic(packetId, data.data(),
                                   static_cast<int>(data.size()));
        }, sessionId_, seq);

        pJobQueue_->Enqueue(std::move(job));

        recvBuffer_.MoveReadPos(packetSize);
    }
}

void Session::ProcessGameLogic(PacketId packetId, const char* pData, int size) {
    // 로직 스레드에서 실행됨
    std::cout << "[Session " << sessionId_ << "] Processing packet: "
              << static_cast<int>(packetId) << "\n";

    switch (packetId) {
        case PacketId::LoginRequest: {
            // 로그인 처리
            if (size < static_cast<int>(sizeof(LoginRequestPacket))) break;

            const LoginRequestPacket* req =
                reinterpret_cast<const LoginRequestPacket*>(pData);

            std::cout << "[Session " << sessionId_
                      << "] Login request from: " << req->username << "\n";

            // 응답 전송
            LoginResponsePacket response{};
            response.header.packetId = PacketId::LoginResponse;
            response.header.size = sizeof(LoginResponsePacket);
            response.success = true;
            response.userId = sessionId_;

            Send(reinterpret_cast<char*>(&response), sizeof(response));
            break;
        }

        case PacketId::ChatMessage: {
            // 채팅 처리
            if (size < static_cast<int>(sizeof(PacketHeader))) break;

            const ChatMessagePacket* req =
                reinterpret_cast<const ChatMessagePacket*>(pData);

            std::cout << "[Chat] User " << sessionId_ << ": "
                      << req->message << "\n";
            break;
        }

        case PacketId::MoveRequest: {
            // 이동 처리
            if (size < static_cast<int>(sizeof(MoveRequestPacket))) break;

            const MoveRequestPacket* req =
                reinterpret_cast<const MoveRequestPacket*>(pData);

            std::cout << "[Session " << sessionId_
                      << "] Move to (" << req->x << ", " << req->y << ")\n";

            // 응답 전송
            MoveResponsePacket response{};
            response.header.packetId = PacketId::MoveResponse;
            response.header.size = sizeof(MoveResponsePacket);
            response.userId = sessionId_;
            response.x = req->x;
            response.y = req->y;

            Send(reinterpret_cast<char*>(&response), sizeof(response));
            break;
        }

        default:
            std::cerr << "[Session " << sessionId_
                      << "] Unknown packet: "
                      << static_cast<int>(packetId) << "\n";
            break;
    }
}

void Session::Send(const char* pData, int size) {
    if (!IsConnected()) return;

    // 송신 데이터 복사
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

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cerr << "[Session " << sessionId_
                      << "] WSASend failed: " << error << "\n";
            delete sendCtx;
            Disconnect();
        }
    }
}

void Session::Disconnect() {
    bool expected = true;
    if (!isConnected_.compare_exchange_strong(expected, false)) {
        return;  // 이미 연결 해제됨
    }

    std::cout << "[Session " << sessionId_ << "] Disconnected\n";

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}
