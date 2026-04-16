
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h>
#include <functional>
#include <queue>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

struct TimerEvent {
    uint64_t timerId;
    std::chrono::steady_clock::time_point expireTime;
    std::function<void()> callback;
    std::chrono::milliseconds interval;
    bool isCancelled;

    bool operator>(const TimerEvent& other) const {
        return expireTime > other.expireTime;
    }
};

struct TimerContext : public OVERLAPPED {
    std::function<void()>* pCallback;

    TimerContext() {
        ZeroMemory(static_cast<OVERLAPPED*>(this), sizeof(OVERLAPPED));
        pCallback = nullptr;
    }
};

const ULONG_PTR COMPLETION_KEY_TIMER = 0xFFFFFFFE;

class TimerManager {
private:
    std::priority_queue<TimerEvent, std::vector<TimerEvent>,
                       std::greater<TimerEvent>> timerQueue_;
    std::unordered_map<uint64_t, bool> cancelledTimers_;

    std::mutex queueMutex_;
    std::condition_variable cv_;
    std::atomic<bool> isRunning_;
    std::thread timerThread_;

    HANDLE hCompletionPort_;
    std::atomic<uint64_t> nextTimerId_;

public:
    TimerManager() : hCompletionPort_(NULL), nextTimerId_(1), isRunning_(false) {}
    ~TimerManager() { Shutdown(); }

    void Initialize(HANDLE hCompletionPort);
    void Shutdown();

    uint64_t AddTimer(std::chrono::milliseconds delay,
                     std::function<void()> callback,
                     std::chrono::milliseconds interval = std::chrono::milliseconds(0));

    void CancelTimer(uint64_t timerId);

private:
    void TimerThreadFunc();
    void PostTimerCallback(const TimerEvent& event);
};
