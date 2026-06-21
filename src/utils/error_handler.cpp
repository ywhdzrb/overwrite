/**
 * @file error_handler.cpp
 * @brief 错误处理系统实现
 *
 * 提供堆栈打印、断言、崩溃信号处理器和 terminate 处理器。
 * 使用 glibc backtrace() + abi::__cxa_demangle 解析 C++ 符号名。
 */

#include "utils/error_handler.hpp"
#include "utils/logger.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>

// POSIX / glibc
#include <unistd.h>
#include <execinfo.h>
#include <cxxabi.h>

namespace owengine {

// ============================================================
// 堆栈跟踪打印
// ============================================================

void printStacktrace(int max_depth) {
    void* buffer[64];
    if (max_depth > 64) max_depth = 64;

    int frames = backtrace(buffer, max_depth);
    if (frames <= 0) return;

    // 获取符号名（可能被 mangled）
    char** symbols = backtrace_symbols(buffer, frames);
    if (!symbols) return;

    // 使用 stderr 作为后备输出（Logger 可能在崩溃时不可用）
    std::stringstream ss;
    ss << "\n===== Stack Trace (" << frames << " frames) =====";

    for (int i = 0; i < frames; i++) {
        std::string frame(symbols[i]);

        // 尝试 demangle C++ 符号名
        // 格式通常为: /path/binary(function+offset) [address]
        size_t openParen = frame.find('(');
        size_t plus = frame.find('+', openParen);
        size_t closeParen = frame.find(')', openParen);

        if (openParen != std::string::npos && plus != std::string::npos &&
            closeParen != std::string::npos) {
            // 提取 mangled 符号名
            std::string mangled = frame.substr(openParen + 1, plus - openParen - 1);

            int status = 0;
            char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);

            if (status == 0 && demangled) {
                std::string addr = frame.substr(plus, closeParen - plus);
                std::string binary = frame.substr(0, openParen);
                ss << "\n  #" << i << " " << demangled << addr;
                std::free(demangled);
            } else {
                ss << "\n  #" << i << " " << frame;
            }
        } else {
            ss << "\n  #" << i << " " << frame;
        }
    }

    ss << "\n====================================";

    // 先尝试用 Logger，如果崩溃导致 Logger 不可用则 fallback 到 cerr
    try {
        Logger::fatal(ss.str());
    } catch (...) {
        std::string _fb = ss.str();
        write(STDERR_FILENO, _fb.data(), _fb.size());
        write(STDERR_FILENO, "\n", 1);
    }

    std::free(symbols);
}

// ============================================================
// 信号处理器
// ============================================================

namespace {

// 保存原始信号处理器（用于 restore，当前不实现恢复逻辑）
struct sigaction old_handlers[5];
int handled_signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
const char* signal_names[] = {"SIGSEGV", "SIGABRT", "SIGFPE", "SIGILL", "SIGBUS"};
constexpr int NUM_SIGNALS = 5;

// 可重入保护：信号处理函数中再次触发信号时直接终止，避免无限递归
static volatile sig_atomic_t g_fatal_reentry_guard = 0;

// 信号安全版本的堆栈打印 —— 仅使用 write() + backtrace() 两个 async-signal-safe 函数
static void signalSafePrintStacktrace(int fd) {
    // 最大帧数，防止栈溢出
    void* buffer[48];

    // backtrace() 本身是 glibc 中 async-signal-safe 的
    int frames = backtrace(buffer, 48);
    if (frames <= 0) return;

    // 逐帧用 write() 输出（避免 backtrace_symbols 堆分配，后者不安全）
    for (int i = 0; i < frames; i++) {
        char line[128];
        int n = snprintf(line, sizeof(line), "  #%d %p\n", i, buffer[i]);
        if (n > 0) {
            write(fd, line, (size_t)(n < (int)sizeof(line) ? n : sizeof(line)));
        }
    }
}

/**
 * @brief 通用信号处理器（async-signal-safe）
 * @note 捕获崩溃信号，打印堆栈跟踪后终止。
 *       不尝试恢复——崩溃信号意味着进程状态不可信。
 *       所有 I/O 操作仅用 write()，确保在信号处理上下文中安全。
 */
void crashSignalHandler(int sig, siginfo_t* info, void* context) {
    (void)context;

    // 可重入保护
    if (g_fatal_reentry_guard) {
        _Exit(EXIT_FAILURE);
    }
    g_fatal_reentry_guard = 1;

    // 只用 write() 输出 —— async-signal-safe
    int fd = STDERR_FILENO;

    // 查找信号名称
    const char* sig_name = "UNKNOWN";
    for (int i = 0; i < NUM_SIGNALS; i++) {
        if (handled_signals[i] == sig) {
            sig_name = signal_names[i];
            break;
        }
    }

    // 手动格式化并 write
    {
        char buf[256];
        int n = snprintf(buf, sizeof(buf),
                         "\n[FATAL] 捕获到崩溃信号: %s (%d)\n"
                         "  信号码: %d\n"
                         "  错误地址: %p\n",
                         sig_name, sig,
                         info ? info->si_code : -1,
                         info ? info->si_addr : nullptr);
        if (n > 0) {
            write(fd, buf, (size_t)(n < (int)sizeof(buf) ? n : sizeof(buf)));
        }
    }

    // 信号安全堆栈打印（仅地址，不做 demangle）
    {
        const char msg[] = "\n===== Signal-Safe Stack Trace (addresses only) =====\n";
        write(fd, msg, sizeof(msg) - 1);
        signalSafePrintStacktrace(fd);
        const char end[] = "===================================================\n";
        write(fd, end, sizeof(end) - 1);
    }

    // 终止进程
    _Exit(EXIT_FAILURE);
}

/**
 * @brief terminate 处理器
 * @note 当未捕获的异常或 std::terminate 被调用时触发。
 *       打印异常信息和堆栈后终止。
 */
void terminateHandler() {
    std::stringstream ss;
    ss << "std::terminate 被调用";

    // 尝试获取异常信息
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            ss << "\n  未捕获的异常: " << e.what();
        } catch (const std::string& s) {
            ss << "\n  未捕获的字符串异常: " << s;
        } catch (...) {
            ss << "\n  未捕获的未知类型异常";
        }
    }

    try {
        Logger::fatal(ss.str());
    } catch (...) {
        std::string _fb = ss.str();
        write(STDERR_FILENO, _fb.data(), _fb.size());
        write(STDERR_FILENO, "\n", 1);
    }

    printStacktrace();
    std::_Exit(EXIT_FAILURE);
}

} // anonymous namespace

// ============================================================
// 安装处理器
// ============================================================

void installCrashHandlers(const std::string& app_name) {
    (void)app_name;

    // 安装信号处理器
    struct sigaction sa{};
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    for (int i = 0; i < NUM_SIGNALS; i++) {
        if (sigaction(handled_signals[i], &sa, &old_handlers[i]) != 0) {
            Logger::warning(std::string("安装信号处理器失败: ") + signal_names[i]);
        }
    }

    // 安装 terminate 处理器
    std::set_terminate(terminateHandler);

    Logger::info("[ErrorHandler] 崩溃信号处理器已安装 (SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS)");
}

} // namespace owengine
