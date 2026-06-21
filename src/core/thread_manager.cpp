#include "core/thread_manager.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>
#include <pthread.h>

namespace owengine {

// ============================================================================
// ThreadProfile 实现
// ============================================================================

/**
 * @brief 从 /proc/cpuinfo 和 sysfs 读取核心拓扑
 */
void ThreadProfile::detectTopology(ThreadProfile& p) {
    long n = sysconf(_SC_NPROCESSORS_CONF);
    p.logical_cores = static_cast<size_t>(n > 0 ? n : 1);

    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::vector<size_t> core_ids;
    while (std::getline(cpuinfo, line)) {
        if (line.compare(0, 9, "core id\t\t: ") == 0) {
            core_ids.push_back(std::stoul(line.substr(9)));
        }
    }
    if (!core_ids.empty()) {
        std::sort(core_ids.begin(), core_ids.end());
        core_ids.erase(std::unique(core_ids.begin(), core_ids.end()), core_ids.end());
        p.physical_cores = core_ids.size();
    } else {
        p.physical_cores = p.logical_cores;
    }

    p.has_smt = (p.logical_cores > p.physical_cores);

    // Hybrid 探测：检查 cluster 拓扑
    std::set<std::string> clusters;
    for (size_t i = 0; i < p.logical_cores; ++i) {
        std::string path = "/sys/devices/system/cpu/cpu"
            + std::to_string(i) + "/topology/cluster_cpus_list";
        std::ifstream cf(path);
        std::string cid;
        if (cf >> cid) clusters.insert(cid);
    }
    if (clusters.size() > 1) {
        p.hybrid.high = p.physical_cores / 2 + p.physical_cores % 2;
        p.hybrid.low  = p.physical_cores / 2;
    } else {
        p.hybrid.high = p.physical_cores;
        p.hybrid.low  = 0;
    }
}

/**
 * @brief 从 sysfs 读取缓存大小
 */
void ThreadProfile::detectCache(ThreadProfile& p) {
    auto readSize = [](int level) -> size_t {
        std::string path = "/sys/devices/system/cpu/cpu0/cache/index"
            + std::to_string(level) + "/size";
        std::ifstream f(path);
        if (!f.is_open()) return 0;
        std::string val;
        f >> val;
        if (val.empty()) return 0;
        size_t mul = 1;
        if      (val.back() == 'K') { mul = 1024;           val.pop_back(); }
        else if (val.back() == 'M') { mul = 1024 * 1024;    val.pop_back(); }
        return static_cast<size_t>(std::stoul(val) * mul);
    };
    p.l1_cache_bytes = readSize(1);
    p.l2_cache_bytes = readSize(2);
    p.l3_cache_bytes = readSize(3);
}

/**
 * @brief FPU + 内存带宽基准测试
 */
void ThreadProfile::runBenchmark(ThreadProfile& p) {
    // FPU：Leibniz π 级数 50M 次迭代
    {
        auto start = std::chrono::high_resolution_clock::now();
        double sum = 0.0;
        for (int64_t i = 0; i < 50000000; ++i) {
            sum += ((i & 1) ? -1.0 : 1.0) / (2.0 * i + 1.0);
        }
        volatile double sink = sum; (void)sink;
        auto end = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(end - start).count();
        p.fpu_score = (50000000.0 / secs) / 1000000.0;
    }
    // 内存带宽：32MB memcpy × 10 次
    {
        size_t sz = 32 * 1024 * 1024;
        auto src = std::make_unique<char[]>(sz);
        auto dst = std::make_unique<char[]>(sz);
        std::memset(src.get(), 0xAA, sz);
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 10; ++i)
            std::memcpy(dst.get(), src.get(), sz);
        volatile char sink = dst.get()[0]; (void)sink;
        auto end = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(end - start).count();
        p.mem_score = (double(sz) * 10.0 / secs) / (1024.0 * 1024.0 * 1024.0);
    }
}

ThreadProfile ThreadProfile::detect() {
    ThreadProfile p;
    detectTopology(p);
    detectCache(p);
    runBenchmark(p);
    return p;
}

ThreadProfile& ThreadProfile::cache() {
    static ThreadProfile inst;
    return inst;
}

const ThreadProfile& ThreadProfile::cached() {
    auto& inst = cache();
    if (inst.logical_cores == 0) {
        inst = detect();
    }
    return inst;
}

// ============================================================================
// ThreadManager 实现
// ============================================================================

ThreadManager::ThreadManager(bool use_benchmark) {
    profile_ = use_benchmark ? ThreadProfile::detect() : ThreadProfile::cached();
    auto alloc = computeThreadAllocation();

    size_t total = 0;
    for (auto& n : alloc) total += n;
    workers_.reserve(total);
    worker_info_.reserve(total);

    size_t idx = 0;
    for (size_t p = 0; p < PRIORITY_LEVELS; ++p) {
        for (size_t t = 0; t < alloc[p]; ++t) {
            workers_.emplace_back(&ThreadManager::workerLoop, this, idx,
                                  static_cast<ThreadPriority>(p));
            worker_info_.push_back({std::thread::id(), "",
                                    static_cast<ThreadPriority>(p)});
            ++idx;
        }
    }
}

ThreadManager::~ThreadManager() { stop(); }

void ThreadManager::stop() {
    if (stop_.exchange(true)) return;
    wake_cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

size_t ThreadManager::pending() const {
    size_t total = 0;
    for (auto& q : queues_) {
        std::lock_guard<std::mutex> lock(q.mutex);
        total += q.tasks.size();
    }
    return total + active_.load();
}

void ThreadManager::waitAll() {
    while (true) {
        bool empty = true;
        for (auto& q : queues_) {
            std::lock_guard<std::mutex> lock(q.mutex);
            if (!q.tasks.empty()) { empty = false; break; }
        }
        if (empty && active_.load() == 0) break;
        std::this_thread::yield();
    }
}

/**
 * @brief 根据 CPU 探测结果计算各优先级线程数
 */
std::array<size_t, PRIORITY_LEVELS> ThreadManager::computeThreadAllocation() {
    std::array<size_t, PRIORITY_LEVELS> a{};
    size_t avail = profile_.logical_cores;
    if (avail > 1) --avail;                      // 为主线程保留

    a[0] = 1;                                     // Critical: 1
    avail = (avail > 1) ? avail - 1 : 0;

    a[1] = std::max<size_t>(1, profile_.physical_cores / 4);  // High
    avail = (avail > a[1]) ? avail - a[1] : 0;

    a[2] = std::max<size_t>(1, profile_.physical_cores / 2);  // Normal
    avail = (avail > a[2]) ? avail - a[2] : 0;

    a[3] = avail / 4;                             // Low
    avail = (avail > a[3]) ? avail - a[3] : 0;

    a[4] = (avail > 0) ? 1 : 0;                   // Idle
    return a;
}

/**
 * @brief 工作线程主循环
 *
 * 流程：
 *   1. 阻塞等待自身队列（CV wait 条件：stop_ 或自身队列非空）
 *   2. 从自身队列取任务
 *   3. 取不到（被同优先级其他线程抢走）则窃取更高优先级任务
 *   4. 执行任务 → 回到 1
 *
 * 当更高优先级任务入队时，enqueue() 通知所有更低优先级 CV，
 * 触发工作窃取。
 */
void ThreadManager::workerLoop(size_t worker_index, ThreadPriority priority) {
    size_t prio = static_cast<size_t>(priority);

    // 设置线程名
    std::string tname = "ow:";
    tname += priorityName(priority);
    if (tname.size() > 15) tname.resize(15);
    setThreadName(tname);

    // Critical 线程绑定核心 0
    if (priority == ThreadPriority::Critical) setThreadAffinity(0);

    worker_info_[worker_index].native_id = std::this_thread::get_id();
    worker_info_[worker_index].name = tname;

    while (true) {
        std::function<void()> task;

        // 第 1 步：优先取自身优先级队列
        {
            std::lock_guard<std::mutex> lock(queues_[prio].mutex);
            if (!queues_[prio].tasks.empty()) {
                task = std::move(queues_[prio].tasks.front());
                queues_[prio].tasks.pop_front();
                pending_tasks_--;
            }
        }

        // 第 2 步：自身无任务 → 从最高优先级开始窃取
        if (!task) {
            task = stealTask(prio);
        }

        // 第 3 步：执行任务
        if (task) {
            active_++;
            task();
            active_--;
            continue;
        }

        // 第 4 步：全部队列为空 → 阻塞等待
        if (stop_) break;

        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_cv_.wait(lock, [this]() { return stop_ || pending_tasks_.load() > 0; });
    }
}

/**
 * @brief 从任意优先级队列窃取任务（从最高优先级开始扫描）
 * @param current_priority 当前线程的优先级下标（仅统计用）
 * @return 任务或 nullptr
 *
 * 从 Critical(0) 开始遍历，优先取更高优先级任务。
 * 如果所有队列都空，返回 nullptr。
 */
std::function<void()> ThreadManager::stealTask(size_t /*current_priority*/) {
    for (size_t p = 0; p < PRIORITY_LEVELS; ++p) {
        std::lock_guard<std::mutex> lock(queues_[p].mutex);
        if (!queues_[p].tasks.empty()) {
            auto task = std::move(queues_[p].tasks.front());
            queues_[p].tasks.pop_front();
            pending_tasks_--;
            return task;
        }
    }
    return nullptr;
}

void ThreadManager::setThreadName(const std::string& name) {
    pthread_setname_np(pthread_self(), name.c_str());
}

void ThreadManager::setThreadAffinity(size_t cpu_index) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_index, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

// ── 通道系统 ──

uint32_t ThreadManager::createChannel(const std::string& name) {
    (void)name;
    auto ch = std::make_shared<Channel<std::vector<uint8_t>>>();
    std::lock_guard<std::mutex> lock(channels_mutex_);
    uint32_t id = next_channel_id_++;
    channels_.push_back(ch);
    return id;
}

void ThreadManager::closeChannel(uint32_t channel_id) {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    if (channel_id < channels_.size()) {
        channels_[channel_id].reset();
    }
}

} // namespace owengine
