
#include "TimerManager.h"
#include "Logger.h"

void TimerManager::Initialize(HANDLE hCompletionPort) {
    hCompletionPort_ = hCompletionPort;
    nextTimerId_ = 1;
    isRunning_ = true;
    timerThread_ = std::thread(&TimerManager::TimerThreadFunc, this);

    LOG_INFO("TimerManager 초기화 완료");
}

void TimerManager::Shutdown() {
    if (!isRunning_) return;

    isRunning_ = false;
    cv_.notify_all();

    if (timerThread_.joinable()) {
        timerThread_.join();
    }

    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!timerQueue_.empty()) {
        timerQueue_.pop();
    }
    cancelledTimers_.clear();

    LOG_INFO("TimerManager 종료");
}

uint64_t TimerManager::AddTimer(std::chrono::milliseconds delay,
                                std::function<void()> callback,
                                std::chrono::milliseconds interval) {
    if (!isRunning_) return 0;

    uint64_t timerId = nextTimerId_.fetch_add(1, std::memory_order_relaxed);
    auto expireTime = std::chrono::steady_clock::now() + delay;

    TimerEvent event{timerId, expireTime, callback, interval, false};

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        timerQueue_.push(event);
    }
    cv_.notify_one();

    return timerId;
}

void TimerManager::CancelTimer(uint64_t timerId) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    cancelledTimers_[timerId] = true;
}

void TimerManager::TimerThreadFunc() {
    while (isRunning_) {
        std::unique_lock<std::mutex> lock(queueMutex_);

        // 큐가 비어있으면 대기
        if (timerQueue_.empty()) {
            cv_.wait(lock, [this] {
                return !isRunning_ || !timerQueue_.empty();
            });

            if (!isRunning_) break;
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        const auto& nextTimer = timerQueue_.top();

        // 다음 타이머가 아직 만료되지 않았으면 대기
        if (nextTimer.expireTime > now) {
            cv_.wait_until(lock, nextTimer.expireTime, [this] {
                return !isRunning_;
            });

            if (!isRunning_) break;
            continue;
        }

        // 만료된 타이머 처리
        TimerEvent event = timerQueue_.top();
        timerQueue_.pop();
        lock.unlock();

        // 취소된 타이머는 무시
        if (cancelledTimers_.count(event.timerId) > 0) {
            continue;
        }

        // IOCP에 타이머 작업 포스팅
        PostTimerCallback(event);

        // 반복 타이머면 다시 추가
        if (event.interval.count() > 0) {
            event.expireTime = std::chrono::steady_clock::now() + event.interval;
            lock.lock();
            timerQueue_.push(event);
        }
    }

    LOG_INFO("타이머 스레드 종료");
}

void TimerManager::PostTimerCallback(const TimerEvent& event) {
    // 힙에 콜백 할당 (워커 스레드에서 해제)
    auto* pCallback = new std::function<void()>(event.callback);

    auto* timerCtx = new TimerContext();
    timerCtx->pCallback = pCallback;

    BOOL result = PostQueuedCompletionStatus(
        hCompletionPort_,
        0,
        COMPLETION_KEY_TIMER,
        reinterpret_cast<LPOVERLAPPED>(timerCtx)
    );

    if (!result) {
        LOG_ERROR("PostQueuedCompletionStatus 실패: {}", GetLastError());
        delete pCallback;
        delete timerCtx;
    }
}
