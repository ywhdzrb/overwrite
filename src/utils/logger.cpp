#include "utils/logger.h"
#include <chrono>
#include <iomanip>
#include <ctime>

namespace owengine {

std::atomic<LogLevel> Logger::minLevel_{LogLevel::DEBUG};
std::ostream* Logger::outputStream_ = &std::cout;

void Logger::setMinLevel(LogLevel level) {
    minLevel_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::getMinLevel() {
    return minLevel_.load(std::memory_order_relaxed);
}

void Logger::setOutputStream(std::ostream& stream) {
    outputStream_ = &stream;
}

void Logger::log(LogLevel level, const std::string& message) {
    if (static_cast<int>(level) < static_cast<int>(minLevel_.load(std::memory_order_relaxed))) {
        return;
    }

    std::string levelStr = getLevelString(level);
    std::string timeStr = getCurrentTime();

    *outputStream_ << "[" << timeStr << "] [" << levelStr << "] " << message << std::endl;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

std::string Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");

    return ss.str();
}

} // namespace owengine