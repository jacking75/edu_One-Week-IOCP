
#include "WorkerThread.h"
#include "ServerGlobal.h"
#include "Session.h"
#include "AcceptThread.h"
#include "Logger.h"

DWORD WINAPI WorkerThread(LPVOID param) {
    LOG_INFO("워커 스레드 시작: ThreadID={}", GetCurrentThreadId());

    while (g_running) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(
            g_iocpHandle,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            INFINITE
        );

        // 종료 신호
        if (completionKey == 0 && overlapped == nullptr) {
            break;
        }

        // Accept 완료 처리
        if (completionKey == COMPLETION_KEY_ACCEPT) {
            AcceptOverlapped* acceptOv = (AcceptOverlapped*)overlapped;
            ProcessAccept(acceptOv, bytesTransferred);
            continue;
        }

        // IOContext 복원
        auto ioContext = reinterpret_cast<IOContext*>(overlapped);
        SessionPtr session = ioContext->session;

        // I/O 실패 처리
        if (!result || bytesTransferred == 0) {
            session->Disconnect();
            delete ioContext;
            continue;
        }

        // I/O 타입별 처리
        switch (ioContext->ioType) {
        case IOType::RECV:
            session->OnRecv(bytesTransferred);
            break;

        case IOType::SEND:
            session->OnSend(bytesTransferred);
            break;
        }

        // IOContext 정리 (shared_ptr 자동 해제)
        delete ioContext;
    }

    LOG_INFO("워커 스레드 종료: ThreadID={}", GetCurrentThreadId());
    return 0;
}
