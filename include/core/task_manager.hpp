#pragma once

/**
 * @file task_manager.hpp
 * @brief 基于线程池 + 工作窃取的异步任务调度器
 *
 * 归属模块：core
 * 核心职责：
 *   1. 管理固定数量的工作线程，循环从队列中取任务执行
 *   2. 支持任意可调用对象的异步提交，返回 std::future
 *   3. 提供 waitAll 阻塞等待所有待处理任务完成
 * 依赖关系：仅 C++ 标准库
 * 关键设计：RAII 生命周期 — 析构时自动 stop 并 join 所有工作线程
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace owengine {

/**
 * @brief 通用线程池任务管理器
 *
 * 生命周期：构造时启动指定数量的工作线程，析构时自动停止并回收。
 * 线程安全：enqueue / waitAll / pending 均可从任意线程调用。
 *
 * 使用示例：
 * @code
 *   TaskManager pool(4);
 *   auto fut = pool.enqueue([](int a, int b) { return a + b; }, 3, 4);
 *   int result = fut.get(); // 7
 *   pool.waitAll();
 * @endcode
 */
class TaskManager {
public:
    /**
     * @brief 构造线程池
     * @param num_threads 工作线程数量，0 表示 std::thread::hardware_concurrency()
     * @note 线程一旦创建立即开始循环等待任务
     */
    explicit TaskManager(size_t num_threads = 0);

    /**
     * @brief 析构时自动 stop 并 join 所有工作线程
     * @note 队列中尚未执行的任务将丢失
     */
    ~TaskManager();

    // 禁止拷贝
    TaskManager(const TaskManager&) = delete;
    TaskManager& operator=(const TaskManager&) = delete;

    // 允许移动（移动后原对象不可用）
    TaskManager(TaskManager&&) noexcept = default;
    TaskManager& operator=(TaskManager&&) noexcept = default;

    /**
     * @brief 提交一个任务到线程池
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型包
     * @param f 可调用对象（函数 / lambda / 仿函数等）
     * @param args 转发给 f 的参数
     * @return std::future<return_type> 用于获取任务返回值或异常
     * @throw std::runtime_error 如果线程池已停止
     *
     * 内部将任务包装为 packaged_task 并放入共享队列，唤醒一个等待中的工作线程。
     * 模板实现必须位于头文件中，因为需要编译期推导返回类型。
     */
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief 阻塞等待所有已入队 + 执行中的任务完成
     * @note 不阻止新任务入队；等待条件为 tasks_ 为空且 active_ == 0
     */
    void waitAll();

    /**
     * @brief 获取当前待处理任务总数（排队中 + 执行中）
     * @return 排队任务数与活跃任务数之和
     */
    size_t pending() const;

    /**
     * @brief 停止所有工作线程
     * @note 设置 stop_ 标志 → notify_all → join 所有线程
     *       调用后 enqueue 将抛出异常；已入队的任务不会被执行
     */
    void stop();

private:
    /** 工作线程集合 */
    std::vector<std::thread> workers_;

    /** 待执行任务队列（FIFO） */
    std::queue<std::function<void()>> tasks_;

    /** 保护 tasks_ 和 stop_ 的互斥锁 */
    mutable std::mutex mutex_;

    /** 用于唤醒工作线程取任务的条件变量 */
    std::condition_variable cv_;

    /** 用于 waitAll 等待全部完成的条件变量 */
    std::condition_variable wait_cv_;

    /** 当前正在执行任务的工作线程数量 */
    std::atomic<size_t> active_{0};

    /** 停止标志 — 为 true 时工作线程退出循环 */
    bool stop_ = false;
};

/**
 * @brief enqueue 模板实现
 *
 * 将可调用对象和参数绑定为 std::packaged_task，返回对应的 std::future。
 * 使用 shared_ptr 管理 packaged_task 生命周期，确保 lambda 捕获有效。
 */
template<typename F, typename... Args>
auto TaskManager::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    // 将任务包装为 packaged_task，bind 绑定参数
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("enqueue on stopped TaskManager");
        }
        // lambda 捕获 shared_ptr，调用 packaged_task 的 operator()
        tasks_.emplace([task]() { (*task)(); });
    }

    cv_.notify_one();
    return result;
}

} // namespace owengine
