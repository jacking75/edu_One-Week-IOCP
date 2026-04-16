
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>

extern HANDLE g_iocpHandle;
extern SOCKET g_listenSocket;
extern bool g_running;

bool InitializeServer();
void ShutdownServer();
