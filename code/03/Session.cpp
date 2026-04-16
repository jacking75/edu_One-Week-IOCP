#include "Session.h"
#include "ServerGlobal.h"
#include "SessionPool.h"
#include <iostream>

Session::Session()
    : socket_(INVALID_SOCKET)
    , sessionId_(0)
    , state_(SessionState::Idle)
    , refCount_(0)
    , sendInProgress_(0)
    , pendingDisconnect_(false)
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
    sendInProgress_ = 0;
    pendingDisconnect_ = false;

    ZeroMemory(&recvOverlapped_.overlapped, sizeof(OVERLAPPED));

    UpdateRecvTime();
}

void Session::Initialize() {
    // IOCP에 소켓 연결
    CreateIoCompletionPort((HANDLE)socket_, g_iocpHandle,
                           (ULONG_PTR)this, 0);

    state_ = SessionState::Connected;

    // 첫 번째 비동기 수신 시작
    PostRecv();

    std::cout << "[Session] 세션 " << sessionId_ << " 연결됨" << std::endl;
}

void Session::Disconnect() {
    SessionState expected = SessionState::Connected;

    if (!state_.compare_exchange_strong(expected, SessionState::Disconnecting)) {
        return;  // 이미 연결 해제 중
    }

    std::cout << "[Session] 세션 " << sessionId_ << " 연결 해제" << std::endl;

    // 소켓 종료
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    state_ = SessionState::Disconnected;

    // 세션 풀에 반환
    g_sessionPool->Release(this);

    // 초기 참조 카운트 감소
    Release();
}

void Session::PostRecv() {
    if (state_ != SessionState::Connected)
        return;

    ZeroMemory(&recvOverlapped_.overlapped, sizeof(OVERLAPPED));

    recvOverlapped_.wsaBuf.buf = (char*)recvBuffer_;
    recvOverlapped_.wsaBuf.len = sizeof(recvBuffer_);

    DWORD flags = 0;
    DWORD bytesReceived = 0;

    AddRef();

    int result = WSARecv(socket_, &recvOverlapped_.wsaBuf, 1,
                         &bytesReceived, &flags,
                         &recvOverlapped_.overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cout << "[Session] WSARecv 실패: " << error << std::endl;
            Release();
            Disconnect();
        }
    }
}

void Session::PostSend(const BYTE* data, int32_t len) {
    if (state_ != SessionState::Connected)
        return;

    SendOverlapped* sendOv = new SendOverlapped;
    ZeroMemory(&sendOv->overlapped, sizeof(OVERLAPPED));
    sendOv->owner = this;
    sendOv->buffer.assign(data, data + len);

    WSABUF wsaBuf;
    wsaBuf.buf = (char*)sendOv->buffer.data();
    wsaBuf.len = static_cast<ULONG>(sendOv->buffer.size());

    sendInProgress_.fetch_add(1);
    AddRef();

    DWORD bytesSent = 0;
    int result = WSASend(socket_, &wsaBuf, 1, &bytesSent, 0,
                         &sendOv->overlapped, nullptr);

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cout << "[Session] WSASend 실패: " << error << std::endl;
            sendInProgress_.fetch_sub(1);
            Release();
            delete sendOv;
            Disconnect();
        }
    }
}

void Session::ProcessRecv(DWORD bytesTransferred) {
    UpdateRecvTime();

    if (bytesTransferred == 0) {
        // 정상 종료
        Disconnect();
        return;
    }

    // Echo 처리 (받은 데이터를 그대로 전송)
    PostSend(recvBuffer_, bytesTransferred);

    // 다음 수신 대기
    PostRecv();
}

void Session::ProcessSend(SendOverlapped* sendOv, DWORD bytesTransferred) {
    delete sendOv;

    int32_t sendCount = sendInProgress_.fetch_sub(1) - 1;

    // 송신 완료 후 연결 해제 대기 중이면
    if (pendingDisconnect_ && sendCount == 0) {
        Disconnect();
    }
}

void Session::AddRef() {
    refCount_.fetch_add(1);
}

void Session::Release() {
    int32_t refCount = refCount_.fetch_sub(1) - 1;
    if (refCount == 0) {
        // 참조 카운트가 0이 되면 완전히 해제
        // 세션 풀이 관리하므로 delete하지 않음
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
