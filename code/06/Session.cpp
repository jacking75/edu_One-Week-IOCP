
#include "Session.h"
#include "ServerGlobal.h"
#include "SessionManager.h"
#include "PacketDispatcher.h"
#include "Logger.h"

Session::Session(SOCKET socket, uint64_t sessionId)
    : socket_(socket)
    , sessionId_(sessionId)
    , state_(SessionState::Idle)
    , refCount_(1)
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

void Session::Initialize() {
    CreateIoCompletionPort((HANDLE)socket_, g_iocpHandle,
                           (ULONG_PTR)this, 0);
    state_ = SessionState::Connected;
    PostRecv();
    LOG_INFO("세션 {} 연결됨", sessionId_);
}

void Session::Disconnect() {
    SessionState expected = SessionState::Connected;
    if (!state_.compare_exchange_strong(expected, SessionState::Disconnecting)) {
        return;
    }

    LOG_INFO("세션 {} 연결 해제 (UserID={})", sessionId_, userID_);

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    state_ = SessionState::Disconnected;
    isAuthenticated_ = false;

    SessionManager::Instance().RemoveSession(static_cast<uint32_t>(sessionId_));

    Release();
}

void Session::PostRecv() {
    if (state_ != SessionState::Connected) return;

    ZeroMemory(&recvOverlapped_.overlapped, sizeof(OVERLAPPED));

    WSABUF wsaBuf;
    if (!recvBuffer_.GetRecvBuffer(wsaBuf)) {
        LOG_ERROR("세션 {} 수신 버퍼 부족", sessionId_);
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
            LOG_ERROR("세션 {} WSARecv 실패: {}", sessionId_, error);
            Release();
            Disconnect();
        }
    }
}

void Session::Send(PacketPtr packet) {
    if (state_ != SessionState::Connected) return;

    if (!sendBuffer_.Write(reinterpret_cast<const BYTE*>(packet->GetBuffer()),
                           static_cast<int>(packet->GetSize()))) {
        LOG_ERROR("세션 {} 송신 버퍼 부족", sessionId_);
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
            LOG_ERROR("세션 {} WSASend 실패: {}", sessionId_, error);
            Release();
            delete sendOv;
            sendRegistered_ = false;
            Disconnect();
        }
    }
}

void Session::ProcessRecv(DWORD bytesTransferred) {
    if (bytesTransferred == 0) {
        Disconnect();
        return;
    }

    recvBuffer_.OnRecvCompleted(bytesTransferred);

    auto packets = ExtractPackets();

    for (auto& packet : packets) {
        PacketDispatcher::Instance().Dispatch(shared_from_this(), packet);
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

std::vector<PacketPtr> Session::ExtractPackets() {
    std::vector<PacketPtr> packets;

    while (recvBuffer_.GetDataSize() >= static_cast<int>(PACKET_HEADER_SIZE)) {
        uint16_t packetSize = 0;
        recvBuffer_.Peek(reinterpret_cast<BYTE*>(&packetSize), sizeof(packetSize));

        if (packetSize < PACKET_HEADER_SIZE) {
            LOG_ERROR("패킷 크기가 헤더보다 작음: Size={}, SessionID={}",
                     packetSize, sessionId_);
            Disconnect();
            break;
        }

        if (packetSize > MAX_PACKET_SIZE) {
            LOG_ERROR("패킷 크기가 최대 크기 초과: Size={}, SessionID={}",
                     packetSize, sessionId_);
            Disconnect();
            break;
        }

        if (recvBuffer_.GetDataSize() < static_cast<int>(packetSize)) {
            break;
        }

        auto packet = std::make_shared<Packet>(packetSize);
        recvBuffer_.Peek(reinterpret_cast<BYTE*>(packet->GetBuffer()), packetSize);
        recvBuffer_.Consume(packetSize);

        packets.push_back(packet);
    }

    return packets;
}

void Session::AddRef() {
    refCount_.fetch_add(1);
}

void Session::Release() {
    int32_t refCount = refCount_.fetch_sub(1) - 1;
    if (refCount == 0) {
        // shared_ptr이 관리
    }
}
