
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>

class SessionPool;

extern HANDLE g_iocpHandle;
extern SessionPool* g_sessionPool;
extern SOCKET g_listenSocket;
extern bool g_running;

bool InitializeServer();
void ShutdownServer();
