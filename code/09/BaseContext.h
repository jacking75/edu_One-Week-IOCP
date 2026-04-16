
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>

enum class ContextType
{
    RECV,
    SEND
};

struct BaseContext
{
    OVERLAPPED overlapped;
    ContextType type;

    BaseContext(ContextType t) : type(t)
    {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};
