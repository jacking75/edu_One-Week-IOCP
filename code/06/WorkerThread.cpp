
#include "WorkerThread.h"
#include "ServerGlobal.h"
#include "Session.h"
#include "AcceptThread.h"
#include "Logger.h"

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

        if (completionKey == 0 && overlapped == nullptr) {
            break;
        }

        if (completionKey == COMPLETION_KEY_ACCEPT) {
            AcceptOverlapped* acceptOv = (AcceptOverlapped*)overlapped;
            ProcessAccept(acceptOv, bytesTransferred);
            continue;
        }

        Session* session = (Session*)completionKey;

        if (result == FALSE || bytesTransferred == 0) {
            session->Disconnect();
            session->Release();
            continue;
        }

        if (overlapped == &session->recvOverlapped_.overlapped) {
            session->ProcessRecv(bytesTransferred);
            session->Release();
        }
        else {
            Session::SendOverlapped* sendOv =
                (Session::SendOverlapped*)overlapped;
            session->ProcessSend(sendOv, bytesTransferred);
            session->Release();
        }
    }

    return 0;
}
