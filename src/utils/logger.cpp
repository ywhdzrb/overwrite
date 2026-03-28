// 日志系统实现
// 提供不同级别的日志记录功能
#include "logger.h"
#include <chrono>
#include <iomanip>
#include <ctime>

namespace vgame {

// 记录日志
// 根据日志级别输出带有时间戳的消息
void Logger::log(LogLevel level, const std::string& message) {
    std::string levelStr = getLevelString(level);
    std::string timeStr = getCurrentTime();
    
    std::cout << "[" << timeStr << "] [" << levelStr << "] " << message << std::endl;
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
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    
    return ss.str();
}

} // namespace vgame