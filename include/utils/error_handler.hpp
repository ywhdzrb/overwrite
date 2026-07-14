#pragma once

/**
 * @file error_handler.hpp
 * @brief 错误处理系统 — 断言宏、堆栈打印、崩溃信号处理器
 *
 * 归属模块：utils
 * 核心职责：
 *   1. OW_ASSERT / OW_UNREACHABLE 断言宏（含文件/行号/函数名）
 *   2. 崩溃信号处理器（SIGSEGV/SIGABRT/SIGFPE/SIGILL → 自动打印堆栈）
 *   3. std::set_terminate 处理器（捕获未处理异常）
 *   4. 堆栈跟踪打印工具
 * 依赖关系：Logger（用于输出），glibc backtrace（POSIX）
 * 关键设计：仅在 OW_DEBUG 构建中启用断言，信号处理器在所有构建中生效
 */

#include <csetjmp>
#include <csignal>
#include <string>

namespace owengine {

// ============================================================
// Vulkan 设备关闭崩溃绕过的信号跳转上下文
// 用于绕过 NVIDIA 580.173.02 在 vkDestroyDevice 时的 GLX 崩溃
// ============================================================
extern sigjmp_buf g_vulkanDeviceCleanupJmpBuf;
extern volatile sig_atomic_t g_vulkanDeviceCleanupActive;

/**
 * @brief 打印当前线程的堆栈跟踪
 * @param max_depth 最大堆栈深度（默认 64）
 * @note 使用 glibc backtrace() + abi::__cxa_demangle 解析 C++ 符号
 */
void printStacktrace(int max_depth = 64);

/**
 * @brief 安装崩溃信号处理器和 terminate 处理器
 * @param app_name 应用名称（用于日志前缀）
 * @note 在 main() 启动时调用一次。
 *       安装 SIGSEGV/SIGABRT/SIGFPE/SIGILL 的处理器，
 *       以及 std::set_terminate 处理器。
 */
void installCrashHandlers(const std::string& app_name = "OverWrite");

} // namespace owengine

// ============================================================
// 断言宏
// ============================================================

/**
 * @brief 断言宏
 * @param condition 条件表达式
 * @param message 失败时的描述信息
 *
 * 当 condition 为 false 时：
 *   - 打印堆栈跟踪
 *   - 通过 Logger::fatal 输出错误信息（含文件/行号/函数名）
 *   - 调用 abort() 终止进程
 *
 * 在 Release 构建中编译为 NOP（条件仍求值以消除编译器警告）
 */
#if defined(OW_DEBUG) || defined(OW_ENABLE_ASSERTS)
#define OW_ASSERT(condition, message)                                                          \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            ::owengine::printStacktrace();                                                     \
            ::owengine::Logger::fatal(                                                          \
                std::string("ASSERT FAILED: ") + (message) +                                    \
                "\n  File: " + __FILE__ +                                                       \
                "\n  Line: " + std::to_string(__LINE__) +                                       \
                "\n  Func: " + __func__);                                                       \
            std::abort();                                                                      \
        }                                                                                      \
    } while (false)
#else
#define OW_ASSERT(condition, message) ((void)(condition))
#endif

/**
 * @brief 不可达代码标记宏
 * @note 标识理论上不可能到达的代码路径。
 *       到达时将打印堆栈并终止。
 */
#if defined(OW_DEBUG) || defined(OW_ENABLE_ASSERTS)
#define OW_UNREACHABLE(message)                                                                \
    do {                                                                                       \
        ::owengine::printStacktrace();                                                         \
        ::owengine::Logger::fatal(                                                              \
            std::string("UNREACHABLE: ") + (message) +                                         \
            "\n  File: " + __FILE__ +                                                          \
            "\n  Line: " + std::to_string(__LINE__) +                                          \
            "\n  Func: " + __func__);                                                          \
        std::abort();                                                                          \
    } while (false)
#else
#define OW_UNREACHABLE(message) __builtin_unreachable()
#endif
