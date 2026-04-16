
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <mutex>
#include <string>
#include <format>
#include <fstream>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Initialize(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        logFile_.open(filename, std::ios::app);
    }

    template<typename... Args>
    void Info(const std::string& fmt, Args&&... args) {
        Log("INFO", fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warning(const std::string& fmt, Args&&... args) {
        Log("WARN", fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(const std::string& fmt, Args&&... args) {
        Log("ERROR", fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Debug(const std::string& fmt, Args&&... args) {
        Log("DEBUG", fmt, std::forward<Args>(args)...);
    }

private:
    Logger() = default;
    ~Logger() { if (logFile_.is_open()) logFile_.close(); }

    template<typename... Args>
    void Log(const char* level, const std::string& fmt, Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string msg = std::vformat(fmt, std::make_format_args(args...));
        std::cout << "[" << level << "] " << msg << std::endl;
        if (logFile_.is_open()) {
            logFile_ << "[" << level << "] " << msg << std::endl;
        }
    }

    std::mutex mutex_;
    std::ofstream logFile_;
};

#define LOG_INFO(...)    Logger::Instance().Info(__VA_ARGS__)
#define LOG_WARNING(...) Logger::Instance().Warning(__VA_ARGS__)
#define LOG_ERROR(...)   Logger::Instance().Error(__VA_ARGS__)
#define LOG_DEBUG(...)   Logger::Instance().Debug(__VA_ARGS__)
