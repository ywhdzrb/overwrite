#include "core/task_manager.hpp"

namespace owengine {

/**
 * @brief 工作线程主循环
 *
 * 每个线程反复执行以下步骤：
 *   1. 加锁等待条件变量（有任务到达或 stop 信号）
 *   2. 取出一个任务，active_ 递增，解锁
 *   3. 执行任务
 *   4. 加锁，active_ 递减，若全部空闲则通知 waitAll
 *
 * 当 stop_ 为 true 且队列为空时线程退出循环。
 */
static void workerLoop(
    std::queue<std::function<void()>>& tasks,
    std::mutex& mutex,
    std::condition_variable& cv,
    std::condition_variable& wait_cv,
    std::atomic<size_t>& active,
    bool& stop)
{
    while (true) {
        std::function<void()> task;
        {
            // 等待条件：stop 或者队列非空
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return stop || !tasks.empty(); });

            // stop 且无剩余任务 → 线程退出
            if (stop && tasks.empty()) {
                return;
            }

            // 从队列前端取出任务
            task = std::move(tasks.front());
            tasks.pop();
            active++;
        }

        // 执行任务（锁外执行，不阻塞其他线程取任务）
        task();

        {
            // 任务完成，active 递减
            std::lock_guard<std::mutex> lock(mutex);
            active--;

            // 如果所有任务都已完成，通知 waitAll
            if (!stop && tasks.empty() && active == 0) {
                wait_cv.notify_all();
            }
        }
    }
}

TaskManager::TaskManager(size_t num_threads) {
    // 0 表示使用硬件并发数（至少为 1）
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 1;
        }
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(workerLoop,
                              std::ref(tasks_),
                              std::ref(mutex_),
                              std::ref(cv_),
                              std::ref(wait_cv_),
                              std::ref(active_),
                              std::ref(stop_));
    }
}

TaskManager::~TaskManager() {
    stop();
}

void TaskManager::waitAll() {
    std::unique_lock<std::mutex> lock(mutex_);
    // 等待所有任务完成（队列为空且没有活跃任务）
    wait_cv_.wait(lock, [this]() {
        return tasks_.empty() && active_ == 0;
    });
}

size_t TaskManager::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size() + active_.load();
}

void TaskManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }

    // 唤醒所有等待任务的工作线程
    cv_.notify_all();

    // 等待所有线程退出
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace owengine
