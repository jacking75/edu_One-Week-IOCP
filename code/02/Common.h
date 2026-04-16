#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <format>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

using SessionID = uint64_t;

// 로거
// Windows.h의 ERROR 매크로와 충돌 방지
#undef ERROR

class Logger {
public:
    enum class Level { DEBUG, INFO, WARN, ERROR };

    template<typename... Args>
    static void Log(Level level, const std::string& fmt, Args&&... args) {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);

        std::cout << "[" << GetLevelString(level) << "] ";
        std::cout << std::vformat(fmt, std::make_format_args(args...)) << "\n";
    }

private:
    static const char* GetLevelString(Level level) {
        switch (level) {
            case Level::DEBUG: return "DEBUG";
            case Level::INFO:  return "INFO";
            case Level::WARN:  return "WARN";
            case Level::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};

#define LOG_DEBUG(...) Logger::Log(Logger::Level::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  Logger::Log(Logger::Level::INFO, __VA_ARGS__)
#define LOG_WARN(...)  Logger::Log(Logger::Level::WARN, __VA_ARGS__)
#define LOG_ERROR(...) Logger::Log(Logger::Level::ERROR, __VA_ARGS__)


// I/O 작업 종류
enum class IOOperation {
    RECV,
    SEND
};

// I/O 컨텍스트
struct IOContext {
    OVERLAPPED overlapped;
    IOOperation operation;
    WSABUF wsaBuf;
    char buffer[4096];

    IOContext() {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        operation = IOOperation::RECV;
        wsaBuf.buf = buffer;
        wsaBuf.len = sizeof(buffer);
    }

    void Reset(IOOperation op) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        operation = op;
        wsaBuf.buf = buffer;
        wsaBuf.len = sizeof(buffer);
    }
};
