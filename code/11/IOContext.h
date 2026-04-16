#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <memory>

enum class IOType {
    Recv,
    Send,
    Accept
};

class Session;

struct IOContext {
    OVERLAPPED overlapped;
    IOType ioType;
    std::shared_ptr<Session> session;

    IOContext() : ioType(IOType::Recv) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};

struct SendContext {
    OVERLAPPED overlapped;
    IOType ioType;
    std::shared_ptr<Session> session;
    std::shared_ptr<std::vector<char>> sharedBuffer;
    char* buffer;
    int size;

    SendContext() : ioType(IOType::Send), buffer(nullptr), size(0) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }

    ~SendContext() {
        delete[] buffer;
    }
};
