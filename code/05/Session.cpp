
#include "Session.h"
#include "ServerGlobal.h"
#include "SessionPool.h"
#include "PacketHandler.h"
#include "PacketReader.h"
#include <iostream>
#include <vector>

Session::Session()
    : socket_(INVALID_SOCKET)
    , sessionId_(0)
    , state_(SessionState::Idle)
    , refCount_(0)
    , recvBuffer_(BufferConfig::DEFAULT_RECV_BUFFER)
    , sendBuffer_(BufferConfig::DEFAULT_SEND_BUFFER)
    , sendRegistered_(false)
{
    ZeroMemory(&recvOverlapped_, sizeof(recvOverlapped_));
    recvOverlapped_.owner = this;
}

Session::~Session() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
    }
}

void Session::Reset(SOCKET socket, uint64_t sessionId) {
    socket_ = socket;
    sessionId_ = sessionId;
    state_ = SessionState::Idle;
    refCount_ = 1;
    sendRegistered_ = false;

    recvBuffer_.Clear();
    sendBuffer_.Clear();

    ZeroMemory(&recvOverlapped_.overlapped, sizeof(OVERLAPPED));

    UpdateRecvTime();
}

void Session::Initialize() {
    CreateIoCompletionPort((HANDLE)socket_, g_iocpHandle,
                           (ULONG_PTR)this, 0);

    state_ = SessionState::Connected;
    PostRecv();

    std::cout << "[Session] 세션 " << sessionId_ << " 연결됨" << std::endl;
}

void Session::Disconnect() {
    SessionState expected = SessionState::Connected;

    if (!state_.compare_exchange_strong(expected, SessionState::Disconnecting)) {
        return;
    }

    std::cout << "[Session] 세션 " << sessionId_ << " 연결 해제" << std::endl;

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    state_ = SessionState::Disconnected;
    g_sessionPool->Release(this);
    Release();
}

void Session::PostRecv() {
    if (state_ != SessionState::Connected)
        return;

    ZeroMemory(&recvOverlapped_.overlapped, sizeof(OVERLAPPED));

    WSABUF wsaBuf;
    if (!recvBuffer_.GetRecvBuffer(wsaBuf)) {
        std::cout << "[Session] 수신 버퍼 부족" << std::endl;
        Disconnect();
        return;
    }

    DWORD flags = 0;
    DWORD bytesReceived = 0;

    AddRef();

    int result = WSARecv(socket_, &wsaBuf, 1, &bytesReceived,
                         &flags, &recvOverlapped_.overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cout << "[Session] WSARecv 실패: " << error << std::endl;
            Release();
            Disconnect();
        }
    }
}

void Session::Send(const BYTE* data, int size) {
    if (state_ != SessionState::Connected)
        return;

    if (!sendBuffer_.Write(data, size)) {
        std::cout << "[Session] 송신 버퍼 부족" << std::endl;
        Disconnect();
        return;
    }

    if (sendRegistered_.exchange(true) == true) {
        return;
    }

    RegisterSend();
}

void Session::RegisterSend() {
    WSABUF wsaBufs[2];
    int bufferCount = sendBuffer_.PrepareSendBuffers(wsaBufs);

    if (bufferCount == 0) {
        sendRegistered_ = false;
        return;
    }

    SendOverlapped* sendOv = new SendOverlapped;
    ZeroMemory(&sendOv->overlapped, sizeof(OVERLAPPED));
    sendOv->owner = this;

    AddRef();

    DWORD bytesSent = 0;
    int result = WSASend(socket_, wsaBufs, bufferCount,
                        &bytesSent, 0,
                        &sendOv->overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cout << "[Session] WSASend 실패: " << error << std::endl;
            Release();
            delete sendOv;
            sendRegistered_ = false;
            Disconnect();
        }
    }
}

void Session::ProcessRecv(DWORD bytesTransferred) {
    UpdateRecvTime();

    if (bytesTransferred == 0) {
        Disconnect();
        return;
    }

    if (!recvBuffer_.OnRecvCompleted(bytesTransferred)) {
        std::cout << "[Session] 수신 버퍼 오버플로우" << std::endl;
        Disconnect();
        return;
    }

    while (true) {
        if (recvBuffer_.GetDataSize() < sizeof(PacketHeader)) {
            break;
        }

        PacketHeader header;
        if (recvBuffer_.GetContinuousDataSize() >= static_cast<int>(sizeof(PacketHeader))) {
            header = *(PacketHeader*)recvBuffer_.GetReadPtr();
        } else {
            recvBuffer_.Peek((BYTE*)&header, sizeof(PacketHeader));
        }

        if (!IsValidPacketSize(header.size)) {
            std::cout << "[Session] 잘못된 패킷 크기: "
                     << header.size << std::endl;
            Disconnect();
            return;
        }

        if (recvBuffer_.GetDataSize() < header.size) {
            break;
        }

        OnPacket(header);

        recvBuffer_.Consume(header.size);
    }

    PostRecv();
}

void Session::ProcessSend(SendOverlapped* sendOv, DWORD bytesTransferred) {
    delete sendOv;

    sendBuffer_.OnSendCompleted(bytesTransferred);

    if (sendBuffer_.GetPendingSize() > 0) {
        RegisterSend();
    } else {
        sendRegistered_ = false;
    }
}

void Session::OnPacket(const PacketHeader& header) {
    if (!IsValidPacketSize(header.size)) {
        std::cout << "[Session] 잘못된 패킷 크기: "
                 << header.size << std::endl;
        Disconnect();
        return;
    }

    if (!IsValidPacketID(header.packetId)) {
        std::cout << "[Session] 잘못된 패킷 ID: "
                 << header.packetId << std::endl;
        Disconnect();
        return;
    }

    std::vector<BYTE> packetData(header.size);
    recvBuffer_.Peek(packetData.data(), header.size);

    PacketReader reader(packetData.data(), header.size);

    PacketID packetId = static_cast<PacketID>(header.packetId);

    switch (packetId) {
        case PacketID::ECHO_REQ:
        {
            EchoPacketHandler handler;
            handler.Handle(this, reader);
            break;
        }
        case PacketID::LOGIN_REQ:
        {
            LoginPacketHandler handler;
            handler.Handle(this, reader);
            break;
        }
        case PacketID::CHAT_REQ:
        {
            ChatPacketHandler handler;
            handler.Handle(this, reader);
            break;
        }
        default:
            std::cout << "[Session] 알 수 없는 패킷: "
                     << header.packetId << std::endl;
            break;
    }
}

void Session::AddRef() {
    refCount_.fetch_add(1);
}

void Session::Release() {
    int32_t refCount = refCount_.fetch_sub(1) - 1;
    if (refCount == 0) {
        // 세션 풀이 관리
    }
}

void Session::UpdateRecvTime() {
    lastRecvTime_ = std::chrono::steady_clock::now();
}

bool Session::IsTimeout(int32_t timeoutSeconds) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - lastRecvTime_).count();
    return elapsed >= timeoutSeconds;
}
