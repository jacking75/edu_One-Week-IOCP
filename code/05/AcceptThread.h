
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <MSWSock.h>
#include <cstdint>

struct AcceptOverlapped {
    OVERLAPPED overlapped;
    SOCKET acceptSocket;
    BYTE buffer[64];
};

const ULONG_PTR COMPLETION_KEY_ACCEPT = 0;
const ULONG_PTR COMPLETION_KEY_SESSION = 1;

void PostAccept();
bool InitializeAccept(const char* ip, uint16_t port);
void ProcessAccept(AcceptOverlapped* acceptOv, DWORD bytesTransferred);
