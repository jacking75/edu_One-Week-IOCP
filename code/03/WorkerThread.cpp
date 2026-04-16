#include "WorkerThread.h"
#include "ServerGlobal.h"
#include "Session.h"
#include "AcceptThread.h"
#include <iostream>

DWORD WINAPI WorkerThread(LPVOID param) {
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

        // Accept 완료
        if (completionKey == COMPLETION_KEY_ACCEPT) {
            AcceptOverlapped* acceptOv = (AcceptOverlapped*)overlapped;
            ProcessAccept(acceptOv, bytesTransferred);
            continue;
        }

        // 세션 I/O 완료
        Session* session = (Session*)completionKey;

        if (result == FALSE || bytesTransferred == 0) {
            // 연결 해제
            session->Disconnect();
            session->Release();
            continue;
        }

        // Recv 완료
        if (overlapped == &session->recvOverlapped_.overlapped) {
            session->ProcessRecv(bytesTransferred);
            session->Release();
        }
        // Send 완료
        else {
            Session::SendOverlapped* sendOv =
                (Session::SendOverlapped*)overlapped;
            session->ProcessSend(sendOv, bytesTransferred);
            session->Release();
        }
    }

    return 0;
}
