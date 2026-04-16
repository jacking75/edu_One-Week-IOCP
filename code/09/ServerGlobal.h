
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>

class TimerManager;

extern HANDLE g_iocpHandle;
extern SOCKET g_listenSocket;
extern bool g_running;
extern TimerManager* g_timerManager;

bool InitializeServer();
void ShutdownServer();
