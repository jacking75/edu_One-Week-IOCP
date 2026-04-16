#include "LogicWorker.h"
#include <iostream>
#include <chrono>

void LogicWorker::WorkerThreadFunc() {
    std::cout << "[LogicWorker " << workerId_ << "] Started\n";

    while (isRunning_) {
        Job job;

        if (!pJobQueue_->Dequeue(job)) {
            // 종료 신호
            break;
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        try {
            if (job.task) {
                job.task();
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[LogicWorker " << workerId_
                      << "] Exception: " << e.what() << "\n";
        }
        catch (...) {
            std::cerr << "[LogicWorker " << workerId_
                      << "] Unknown exception\n";
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count();

        processedJobCount_.fetch_add(1, std::memory_order_relaxed);
        totalProcessingTimeUs_.fetch_add(elapsedUs, std::memory_order_relaxed);

        // 너무 오래 걸린 작업 경고 (10ms 이상)
        if (elapsedUs > 10000) {
            std::cout << "[LogicWorker " << workerId_
                      << "] Long job detected: "
                      << (elapsedUs / 1000.0) << "ms (SessionId: "
                      << job.sessionId << ")\n";
        }
    }

    std::cout << "[LogicWorker " << workerId_ << "] Exiting. "
              << "Processed " << processedJobCount_ << " jobs.\n";
}
