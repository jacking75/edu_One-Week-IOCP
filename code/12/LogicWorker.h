#pragma once
#include <thread>
#include <atomic>
#include "JobQueue.h"

class LogicWorker {
private:
    std::thread thread_;
    JobQueue* pJobQueue_;
    std::atomic<bool> isRunning_;
    int workerId_;
    std::atomic<uint64_t> processedJobCount_;
    std::atomic<uint64_t> totalProcessingTimeUs_;

public:
    LogicWorker(int id, JobQueue* pQueue)
        : workerId_(id), pJobQueue_(pQueue), isRunning_(false)
        , processedJobCount_(0), totalProcessingTimeUs_(0) {}
    ~LogicWorker() { Stop(); }

    void Start() {
        isRunning_ = true;
        thread_ = std::thread(&LogicWorker::WorkerThreadFunc, this);
    }
    void Stop() {
        isRunning_ = false;
        if (thread_.joinable()) thread_.join();
    }
    uint64_t GetProcessedJobCount() const {
        return processedJobCount_.load(std::memory_order_relaxed);
    }
    double GetAverageProcessingTimeMs() const {
        uint64_t count = processedJobCount_.load(std::memory_order_relaxed);
        if (count == 0) return 0.0;
        return static_cast<double>(totalProcessingTimeUs_.load(std::memory_order_relaxed))
               / (count * 1000.0);
    }

private:
    void WorkerThreadFunc();
};
