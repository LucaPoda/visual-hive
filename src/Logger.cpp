// Logger.cpp
#include "Logger.h"
#include <filesystem>
#include <ctime>

namespace fs = std::filesystem;

Logger::Logger() : initialized(false) {}

Logger::~Logger() {
    close();
}

void Logger::init(const std::string& logDir, const std::string& baseName) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (initialized) return;

    try {
        if (!fs::exists(logDir)) {
            fs::create_directories(logDir);
        }

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << logDir << "/" << baseName << "_" 
           << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".log";
        currentLogPath = ss.str();

        logFile.open(currentLogPath, std::ios::out | std::ios::app);
        
        // Crea o sovrascrive anche il puntatore "latest.log"
        std::ofstream latestFile(logDir + "/" + baseName + "_latest.log", std::ios::out | std::ios::trunc);
        if (latestFile.is_open()) {
            latestFile << "Active log session: " << currentLogPath << "\n";
            latestFile.close();
        }

        initialized = logFile.is_open();

        if (initialized) {
            logFile << "================================================================================\n";
            logFile << "VisualHive Log Session Started: " << currentLogPath << "\n";
            logFile << "Time: " << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S") 
                    << "." << std::setfill('0') << std::setw(3) << ms.count() << "\n";
            logFile << "================================================================================\n";
            logFile.flush();
            std::cout << "[LOGGER] Log session initialized at: " << currentLogPath << "\n";
        } else {
            std::cerr << "[LOGGER ERROR] Failed to open log file: " << currentLogPath << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[LOGGER ERROR] Exception during init: " << e.what() << "\n";
    }
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG_LVL: return "DEBUG";
        case LogLevel::INFO_LVL:  return "INFO ";
        case LogLevel::WARN_LVL:  return "WARN ";
        case LogLevel::ERROR_LVL: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::getThreadIdString() {
    std::stringstream ss;
    ss << "0x" << std::hex << std::this_thread::get_id();
    return ss.str();
}

std::string Logger::getFormattedTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::log(LogLevel level, const std::string& tag, const std::string& message, const char* file, int line) {
    std::lock_guard<std::mutex> lock(logMutex);

    std::string timestamp = getFormattedTimestamp();
    std::string threadId = getThreadIdString();
    std::string lvlStr = levelToString(level);

    std::stringstream ss;
    ss << "[" << timestamp << "] "
       << "[" << lvlStr << "] "
       << "[TID: " << threadId << "] "
       << "[" << tag << "] "
       << message;

    if (level == LogLevel::ERROR_LVL || level == LogLevel::WARN_LVL) {
        if (file && line > 0) {
            fs::path p(file);
            ss << " (at " << p.filename().string() << ":" << line << ")";
        }
    }

    std::string logLine = ss.str();

    if (initialized && logFile.is_open()) {
        logFile << logLine << "\n";
        if (level == LogLevel::ERROR_LVL || level == LogLevel::WARN_LVL) {
            logFile.flush();
        }
    }

    // Mirror su console per visibilità immediata
    if (level == LogLevel::ERROR_LVL) {
        std::cerr << logLine << "\n";
    } else {
        std::cout << logLine << "\n";
    }
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(logMutex);
    if (initialized && logFile.is_open()) {
        logFile << "================================================================================\n";
        logFile << "VisualHive Log Session Closed gracefully\n";
        logFile << "================================================================================\n";
        logFile.flush();
        logFile.close();
    }
    initialized = false;
}
