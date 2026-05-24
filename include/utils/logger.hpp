#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <atomic>

namespace owengine {

/**
 * @brief 日志级别枚举
 * 
 * 数值越大，级别越高。设置最低日志级别后，
 * 低于该级别的日志将被过滤掉。
 */
enum class LogLevel : int {
    DEBUG   = 0,
    INFO    = 1,
    WARNING = 2,
    ERROR   = 3
};

/**
 * @brief 简单同步日志系统
 * 
 * 提供分级日志输出，支持运行时过滤最低日志级别。
 * 
 * 线程安全：所有公开方法均为原子操作。
 * 生命周期：全局静态，无需手动管理。
 * 
 * 使用示例：
 * @code
 * Logger::setMinLevel(LogLevel::INFO);   // 过滤 DEBUG
 * Logger::debug("这个不会输出");           // 被过滤
 * Logger::info("查看所有光源已加载");       // 输出
 * @endcode
 */
class Logger {
public:
    /// @brief 设置最低输出级别，低于此级别的日志被过滤
    static void setMinLevel(LogLevel level);

    /// @brief 获取当前最低输出级别
    static LogLevel getMinLevel();

    /// @brief 带级别过滤的通用日志输出
    static void log(LogLevel level, const std::string& message);

    // —— 便捷方法（内部调用 log()）——

    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

    /// @brief 定义日志输出的目标流（默认 std::cout）
    static void setOutputStream(std::ostream& stream);

private:
    static std::string getLevelString(LogLevel level);
    static std::string getCurrentTime();

    static std::atomic<LogLevel> minLevel_;
    static std::ostream* outputStream_;
};

} // namespace owengine