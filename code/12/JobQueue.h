#pragma once
#include <concurrent_queue.h>
#include <functional>
#include <atomic>
#include <semaphore>

struct Job {
    std::function<void()> task;
    uint64_t sessionId;
    uint64_t sequenceNumber;

    Job() : sessionId(0), sequenceNumber(0) {}
    Job(std::function<void()> t, uint64_t sid, uint64_t seq)
        : task(std::move(t)), sessionId(sid), sequenceNumber(seq) {}
};

class JobQueue {
private:
    concurrency::concurrent_queue<Job> queue_;
    std::counting_semaphore<1000> semaphore_{0};
    std::atomic<bool> isShutdown_;
    std::atomic<uint64_t> totalEnqueued_;
    std::atomic<uint64_t> totalDequeued_;

public:
    JobQueue() : isShutdown_(false), totalEnqueued_(0), totalDequeued_(0) {}

    void Enqueue(Job job) {
        if (isShutdown_.load(std::memory_order_acquire)) return;
        queue_.push(std::move(job));
        totalEnqueued_.fetch_add(1, std::memory_order_relaxed);
        semaphore_.release();
    }

    bool Dequeue(Job& outJob) {
        semaphore_.acquire();
        if (isShutdown_.load(std::memory_order_acquire)) return false;
        bool success = queue_.try_pop(outJob);
        if (success) totalDequeued_.fetch_add(1, std::memory_order_relaxed);
        return success;
    }

    size_t GetPendingCount() const {
        return totalEnqueued_.load(std::memory_order_relaxed) -
               totalDequeued_.load(std::memory_order_relaxed);
    }

    void Shutdown() {
        isShutdown_.store(true, std::memory_order_release);
        for (int i = 0; i < 100; ++i) semaphore_.release();
    }
};
