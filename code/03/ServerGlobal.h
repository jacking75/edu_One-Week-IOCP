#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>

class SessionPool;

// 전역 변수
extern HANDLE g_iocpHandle;
extern SessionPool* g_sessionPool;
extern SOCKET g_listenSocket;
extern bool g_running;

// 전역 함수
bool InitializeServer();
void ShutdownServer();
