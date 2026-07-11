#pragma once

/**
 * @file thread_pool.hpp
 * @brief 固定线程池 — 替代 std::async 批量任务场景
 *
 * 核心设计：
 * - 固定 N 个工作线程（默认 hardware_concurrency），任务队列 + 条件变量
 * - enqueue() 返回 std::future，兼容现有 std::async 调用点
 * - RAII：析构时停止所有线程，等待进行中任务完成
 * - 线程安全：队列操作全加锁
 *
 * 适用场景：
 * - 地形区块生成（TerrainRenderer）：每帧最多 maxChunksPerFrame 个异步任务
 * - 草区块生成（GrassSystem）：玩家跨边界时批量启动
 *
 * 使用前检查 ThreadPool 是否已初始化（isInitialized()），未初始化时回退 std::async。
 * 线程开销已在构造函数中一次性完成，运行时 enqueue 无线程创建开销。
 */

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

namespace owengine {

class ThreadPool {
public:
    /**
     * @brief 创建线程池
     * @param numThreads 工作线程数，默认 hardware_concurrency()
     *
     * 立即创建 numThreads 个线程，每个线程循环从队列取任务执行。
     * 如果 hardware_concurrency() 返回 0 或 1，确保至少 1 个线程。
     * 线程池为空（numThreads=0）时 enqueue 退化为同步执行。
     */
    explicit ThreadPool(size_t numThreads = 0)
        : stop_(false)
    {
        if (numThreads == 0) {
            unsigned int hc = std::thread::hardware_concurrency();
            numThreads = hc > 0 ? static_cast<size_t>(hc) : 2;
        }
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex_);
                        condition_.wait(lock, [this] {
                            return stop_.load(std::memory_order_acquire) || !tasks_.empty();
                        });
                        if (stop_.load(std::memory_order_acquire) && tasks_.empty())
                            return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    /// 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// 禁止移动（workers_ 持有 std::thread，移动后析构会异常）
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief 析构：通知所有线程停止，等待任务完成
     *
     * 先拉 stop_ 标志，再 notify_all 唤醒所有休眠线程。
     * 每个线程检测到 stop_ && tasks_.empty() 后退出。
     * 析构函数会阻塞直到所有线程 join。
     */
    ~ThreadPool() {
        stop_.store(true, std::memory_order_release);
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /**
     * @brief 提交任务到线程池
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 参数
     * @return std::future<return_type> 任务结果
     *
     * 线程安全：内部加锁 push 到任务队列。
     * 如果线程池已停止，抛出 std::runtime_error。
     * 如果线程池为空（0 线程），在当前线程同步执行并返回已就绪的 future。
     */
    template<class F, class... Args>
    [[nodiscard]] auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        if (workers_.empty()) {
            std::promise<return_type> promise;
            try {
                if constexpr (std::is_void_v<return_type>) {
                    std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
                    promise.set_value();
                } else {
                    promise.set_value(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
                }
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
            return promise.get_future();
        }

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (stop_.load(std::memory_order_acquire)) {
                throw std::runtime_error("ThreadPool: enqueue on stopped pool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return result;
    }

    /** @brief 当前工作线程数 */
    [[nodiscard]] size_t threadCount() const noexcept { return workers_.size(); }

    /** @brief 队列中等待的任务数（调试/监控用） */
    [[nodiscard]] size_t pendingTasks() const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return tasks_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

} // namespace owengine

