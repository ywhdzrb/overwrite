#pragma once

/**
 * @file thread_manager.hpp
 * @brief 高级线程管理模块 — 自动性能探测、多优先级线程池、跨线程通信
 *
 * 归属模块：core
 * 核心职责：
 *   1. CPU 性能自动探测（核心拓扑、缓存、跑分），按优先级分配合适线程数
 *   2. 多优先级任务队列 + 工作窃取，线程命名 + 核心绑定
 *   3. Channel<T> 跨线程消息通道 + MainThreadDispatcher 主线程调度
 * 依赖关系：仅 C++ 标准库 / Linux pthread
 * 线程安全：全部公有接口均可从任意线程调用
 */

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace owengine {

// ============================================================================
// 1. CPU 性能探测结构
// ============================================================================

/**
 * @brief CPU 拓扑与性能探测结果
 * @note detect() 静态方法在启动时调用一次，结果缓存复用
 */
struct ThreadProfile {
    size_t physical_cores{0};          ///< 物理核心数
    size_t logical_cores{0};           ///< 逻辑核心数（含超线程）
    size_t l1_cache_bytes{0};          ///< L1 数据缓存大小
    size_t l2_cache_bytes{0};          ///< L2 缓存大小
    size_t l3_cache_bytes{0};          ///< L3 缓存大小
    double fpu_score{0.0};             ///< FPU 基准分数（百万浮点运算/秒）
    double mem_score{0.0};             ///< 内存带宽基准分数（GB/s）
    bool has_smt{false};               ///< 是否支持超线程

    struct {
        size_t high{0};                ///< 高性能核心数（P-cores）
        size_t low{0};                 ///< 低功耗核心数（E-cores）
    } hybrid;

    /** @brief 执行一次完整的 CPU 探测与基准测试 */
    static ThreadProfile detect();

    /** @brief 获取已缓存的探测结果（首次调用自动探测） */
    static const ThreadProfile& cached();

private:
    static ThreadProfile& cache();
    static void detectTopology(ThreadProfile& p);
    static void detectCache(ThreadProfile& p);
    static void runBenchmark(ThreadProfile& p);
};

// ============================================================================
// 2. 线程优先级枚举
// ============================================================================

/**
 * @brief 任务优先级，Critical 最高，Idle 最低
 */
enum class ThreadPriority : uint8_t {
    Critical = 0,  ///< 实时型（音频、物理触控）
    High     = 1,  ///< 高优先级（网络 I/O、渲染辅助）
    Normal   = 2,  ///< 普通（资源加载、AI 更新）
    Low      = 3,  ///< 低优先级（资产流式加载）
    Idle     = 4,  ///< 空闲（预计算、分析统计）
    Count
};

inline constexpr size_t PRIORITY_LEVELS = static_cast<size_t>(ThreadPriority::Count);

/** @brief 获取优先级对应的中文名称 */
inline const char* priorityName(ThreadPriority p) {
    static constexpr const char* names[] = {"Critical", "High", "Normal", "Low", "Idle"};
    return names[static_cast<size_t>(p)];
}

// ============================================================================
// 3. Channel — 跨线程消息通道
// ============================================================================

/**
 * @brief 线程安全的消息通道（多生产者/单消费者）
 *
 * 模板参数 T 必须可拷贝或移动。
 * 典型用法：生产者线程 send()，消费者线程 receive() 阻塞等待。
 *
 * @tparam T 消息类型
 */
template<typename T>
class Channel {
public:
    using value_type = T;

    Channel() = default;
    ~Channel() = default;

    // 禁止拷贝
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // 允许移动
    Channel(Channel&&) noexcept = default;
    Channel& operator=(Channel&&) noexcept = default;

    /**
     * @brief 发送消息（非阻塞）
     * @param item 消息值
     * @return true 发送成功，false 通道已关闭
     */
    bool send(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return false;
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
        return true;
    }

    /**
     * @brief 阻塞接收消息
     * @return 消息值；若通道已关闭且队列为空，返回 std::nullopt
     */
    std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    /**
     * @brief 尝试接收消息（非阻塞）
     * @param[out] item 写入接收到的消息
     * @return true 成功收到消息，false 队列为空或通道已关闭
     */
    bool tryReceive(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    /** @brief 当前队列深度 */
    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /** @brief 通道是否仍处于打开状态 */
    [[nodiscard]] bool isOpen() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !closed_;
    }

    /** @brief 关闭通道，唤醒所有阻塞的 receive() */
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    std::deque<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
};

// ============================================================================
// 4. MainThreadDispatcher — 主线程任务分发
// ============================================================================

/**
 * @brief 主线程任务分发器
 *
 * 允许任意工作线程将任务调度到主线程执行，适用于：
 * - Vulkan 资源创建/销毁（必须发生在拥有 GPU 队列的线程）
 * - ImGui 更新
 * - GameSession 状态修改
 *
 * 使用方式：worker 线程调用 dispatch() 入队，主线程每帧调用 flush() 执行。
 */
class MainThreadDispatcher {
public:
    MainThreadDispatcher() = default;
    ~MainThreadDispatcher() = default;

    // 禁止拷贝
    MainThreadDispatcher(const MainThreadDispatcher&) = delete;
    MainThreadDispatcher& operator=(const MainThreadDispatcher&) = delete;

    /**
     * @brief 调度一个任务到主线程执行
     * @param fn 可调用对象
     * @tparam F 函数类型
     *
     * 线程安全，可跨线程调用。
     */
    template<typename F>
    void dispatch(F&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace_back(std::forward<F>(fn));
    }

    /**
     * @brief 执行所有待处理的主线程任务（每帧在主线程调用一次）
     */
    void flush() {
        std::deque<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(queue_);
        }
        for (auto& task : batch) {
            task();
        }
    }

    /** @brief 执行至多一个主线程任务 */
    bool flushOne() {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) return false;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
        return true;
    }

    /** @brief 当前排队的主线程任务数 */
    [[nodiscard]] size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /** @brief 清空所有待处理任务 */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

private:
    std::deque<std::function<void()>> queue_;
    mutable std::mutex mutex_;
};

// ============================================================================
// 5. ThreadManager — 高级线程管理器
// ============================================================================

/**
 * @brief 高级线程管理器
 *
 * 基于 ThreadProfile 自动分配每个优先级的工作线程数：
 * | 优先级   | 说明               | 线程数分配策略                        |
 * |----------|--------------------|---------------------------------------|
 * | Critical | 音频、物理触控     | 独占 1 个线程 + 0 号核心绑定          |
 * | High     | 网络、渲染辅助     | 物理核心 × 0.25，至少 1               |
 * | Normal   | 资源加载、AI       | 物理核心 × 0.50，至少 1               |
 * | Low      | 流式资产加载       | 逻辑核心 × 0.25，至少 0               |
 * | Idle     | 预计算、统计       | 1 个线程（仅在无其他任务时运行）       |
 *
 * 生命周期：构造时启动线程池，析构时自动停止。
 * 线程安全：所有公有接口均可从任意线程调用。
 *
 * 功能特性：
 * - CPU 性能自动探测 + 基准测试
 * - 多优先级任务队列 + 工作窃取（空闲线程可窃取更高优先级任务）
 * - 线程命名 + 核心绑定（pthread）
 * - Channel 跨线程消息通道
 * - MainThreadDispatcher 主线程调度
 */
class ThreadManager {
public:
    /**
     * @brief 构造线程管理器
     * @param use_benchmark 是否运行 CPU 基准测试（首次慢 50ms 左右）
     * @note 会自动读取 ThreadProfile::cached()，阻塞直至所有工作线程启动
     */
    explicit ThreadManager(bool use_benchmark = true);

    /**
     * @brief 析构，stop() 并 join 所有工作线程
     */
    ~ThreadManager();

    // 禁止拷贝
    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;

    // 禁止移动（内部线程引用自身栈）
    ThreadManager(ThreadManager&&) = delete;
    ThreadManager& operator=(ThreadManager&&) = delete;

    // ── 任务提交 ──

    /**
     * @brief 提交一个任务到指定优先级队列
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型包
     * @param priority 优先级
     * @param f 可调用对象
     * @param args 参数
     * @return std::future<return_type>
     *
     * 线程安全，可从任意线程调用。
     */
    template<typename F, typename... Args>
    auto enqueue(ThreadPriority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // ── 线程状态 ──

    /** @brief 获取各优先级队列中的待处理任务总数 */
    size_t pending() const;

    /** @brief 获取当前活跃（执行中）任务数 */
    size_t active() const { return active_.load(); }

    /** @brief 获取总工作线程数 */
    size_t threadCount() const { return workers_.size(); }

    /** @brief 获取 CPU 探测结果 */
    const ThreadProfile& profile() const { return profile_; }

    // ── 生命周期 ──

    /** @brief 阻塞等待所有待处理任务完成 */
    void waitAll();

    /** @brief 停止所有工作线程 */
    void stop();

    /** @brief 线程池是否仍在运行 */
    bool isRunning() const { return !stop_.load(); }

    // ── 通信设施 ──

    /** @brief 主线程分发器引用 */
    MainThreadDispatcher& mainDispatcher() { return main_dispatcher_; }
    const MainThreadDispatcher& mainDispatcher() const { return main_dispatcher_; }

    /**
     * @brief 创建一个命名的消息通道
     * @param name 通道名称（仅用于日志/调试）
     * @return 通道 ID
     */
    uint32_t createChannel(const std::string& name = "");

    /**
     * @brief 向指定通道发送消息
     * @tparam T 消息类型（必须匹配通道创建时约定的类型）
     */
    template<typename T>
    bool sendTo(uint32_t channel_id, T&& msg);

    /**
     * @brief 从指定通道接收消息
     * @tparam T 消息类型
     */
    template<typename T>
    std::optional<T> receiveFrom(uint32_t channel_id);

    /**
     * @brief 关闭指定通道
     */
    void closeChannel(uint32_t channel_id);

private:
    // 每个优先级队列的数据
    struct PriorityQueue {
        std::deque<std::function<void()>> tasks;
        mutable std::mutex mutex;
    };

    // 工作线程元数据
    struct WorkerInfo {
        std::thread::id native_id;
        std::string name;
        ThreadPriority priority;
    };

    /** @brief 工作线程主循环 */
    void workerLoop(size_t worker_index, ThreadPriority priority);

    /** @brief 工作窃取：从更高优先级队列偷任务 */
    std::function<void()> stealTask(size_t current_priority);

    /** @brief 设置线程名称 */
    static void setThreadName(const std::string& name);

    /** @brief 设置 CPU 亲和性 */
    static void setThreadAffinity(size_t cpu_index);

    /** @brief 计算各优先级通道的线程数 */
    std::array<size_t, PRIORITY_LEVELS> computeThreadAllocation();

    // 线程池
    std::vector<std::thread> workers_;
    std::vector<WorkerInfo> worker_info_;
    std::array<PriorityQueue, PRIORITY_LEVELS> queues_;

    // 全局状态
    std::atomic<size_t> active_{0};
    std::atomic<size_t> pending_tasks_{0};
    std::atomic<bool> stop_{false};
    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    ThreadProfile profile_;

    // 主线程分发器
    MainThreadDispatcher main_dispatcher_;

    // 通道系统
    struct NamedChannel {
        std::string name;
        void* channel_ptr;           ///< 类型擦除的 Channel 指针
        std::function<void()> deleter;
    };
    std::vector<std::shared_ptr<void>> channels_;
    mutable std::mutex channels_mutex_;
    std::atomic<uint32_t> next_channel_id_{0};

    // 工作窃取计数（统计）
    std::atomic<size_t> steal_count_{0};
};

// ── 模板实现 ──

template<typename F, typename... Args>
auto ThreadManager::enqueue(ThreadPriority priority, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    size_t p = static_cast<size_t>(priority);
    {
        std::lock_guard<std::mutex> lock(queues_[p].mutex);
        if (stop_) {
            throw std::runtime_error("enqueue on stopped ThreadManager");
        }
        queues_[p].tasks.emplace_back([task]() { (*task)(); });
    }

    pending_tasks_++;
    wake_cv_.notify_one();
    return result;
}

template<typename T>
bool ThreadManager::sendTo(uint32_t channel_id, T&& msg) {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (channel_id >= channels_.size()) return false;
    auto channel = std::static_pointer_cast<Channel<std::decay_t<T>>>(channels_[channel_id]);
    return channel->send(std::forward<T>(msg));
}

template<typename T>
std::optional<T> ThreadManager::receiveFrom(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (channel_id >= channels_.size()) return std::nullopt;
    auto channel = std::static_pointer_cast<Channel<T>>(channels_[channel_id]);
    return channel->receive();
}

} // namespace owengine
