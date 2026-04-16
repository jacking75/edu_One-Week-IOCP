
#include "WorkerThread.h"
#include "ServerGlobal.h"
#include "BaseContext.h"
#include "IOContext.h"
#include "SendContext.h"
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
        if (completionKey == 0 && overlapped == nullptr)
            break;

        // Accept 완료 처리
        if (completionKey == COMPLETION_KEY_ACCEPT) {
            AcceptOverlapped* acceptOv = (AcceptOverlapped*)overlapped;
            ProcessAccept(acceptOv, bytesTransferred);
            continue;
        }

        // BaseContext로 타입 확인
        auto baseContext = reinterpret_cast<BaseContext*>(overlapped);

        // I/O 실패 처리
        if (!result || bytesTransferred == 0) {
            if (baseContext->type == ContextType::RECV) {
                auto ioContext = static_cast<IOContext*>(baseContext);
                ioContext->session->Disconnect();
                delete ioContext;
            }
            else if (baseContext->type == ContextType::SEND) {
                auto sendContext = static_cast<SendContext*>(baseContext);
                sendContext->session->Disconnect();
                delete sendContext;
            }
            continue;
        }

        // 타입별 처리
        if (baseContext->type == ContextType::RECV) {
            auto ioContext = static_cast<IOContext*>(baseContext);
            ioContext->session->OnRecv(bytesTransferred);
            delete ioContext;
        }
        else if (baseContext->type == ContextType::SEND) {
            auto sendContext = static_cast<SendContext*>(baseContext);
            sendContext->session->OnSend(sendContext, bytesTransferred);
            // OnSend에서 delete됨
        }
    }

    LOG_INFO("워커 스레드 종료: ThreadID={}", GetCurrentThreadId());
    return 0;
}
