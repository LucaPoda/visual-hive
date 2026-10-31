// Logger.h - Thread-safe, high-detail logging system for VisualHive
#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <thread>

enum class LogLevel {
    DEBUG_LVL,
    INFO_LVL,
    WARN_LVL,
    ERROR_LVL
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& logDir = "logs", const std::string& baseName = "visualhive");
    void log(LogLevel level, const std::string& tag, const std::string& message, const char* file = nullptr, int line = 0);
    void close();

    bool isInitialized() const { return initialized; }

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream logFile;
    std::mutex logMutex;
    bool initialized = false;
    std::string currentLogPath;

    std::string levelToString(LogLevel level);
    std::string getThreadIdString();
    std::string getFormattedTimestamp();
};

#define LOG_DEBUG(tag, msg) Logger::getInstance().log(LogLevel::DEBUG_LVL, tag, msg, __FILE__, __LINE__)
#define LOG_INFO(tag, msg)  Logger::getInstance().log(LogLevel::INFO_LVL,  tag, msg, __FILE__, __LINE__)
#define LOG_WARN(tag, msg)  Logger::getInstance().log(LogLevel::WARN_LVL,  tag, msg, __FILE__, __LINE__)
#define LOG_ERROR(tag, msg) Logger::getInstance().log(LogLevel::ERROR_LVL, tag, msg, __FILE__, __LINE__)
