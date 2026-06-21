#pragma once

/**
 * @file memory_manager.hpp
 * @brief 内存管理器 — 对象池 + 分配统计 + 指针泄漏检测
 *
 * 归属模块：core
 * 核心职责：
 *   1. ObjectPool<T> 固定容量对象池，预分配回收复用
 *   2. AllocStats 全局分配统计（总分配/释放/峰值）
 *   3. PtrRegistry 弱指针注册表，关闭时泄漏检测
 * 关键设计：ObjectPool 线程安全，允许多线程 acquire/release
 */

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <sstream>

namespace owengine {

// ============================================================================
// 分配统计
// ============================================================================

/**
 * @brief 全局内存分配统计
 *
 * 追踪堆内存的分配/释放次数与字节数，用于性能分析和泄漏排查。
 * 需配合 MemoryManager 的 hookNewDelete() 或在分配点手动调用。
 */
struct AllocStats {
    std::atomic<uint64_t> totalAllocations{0};
    std::atomic<uint64_t> totalDeallocations{0};
    std::atomic<size_t> currentBytes{0};
    std::atomic<size_t> peakBytes{0};
    std::atomic<uint64_t> liveObjects{0};

    /** @brief 记录一次分配 */
    void onAlloc(size_t bytes) noexcept {
        totalAllocations.fetch_add(1, std::memory_order_relaxed);
        liveObjects.fetch_add(1, std::memory_order_relaxed);
        size_t prev = currentBytes.fetch_add(bytes, std::memory_order_relaxed);
        size_t newBytes = prev + bytes;
        size_t peak = peakBytes.load(std::memory_order_relaxed);
        while (newBytes > peak && !peakBytes.compare_exchange_weak(peak, newBytes, std::memory_order_relaxed)) {}
    }

    /** @brief 记录一次释放 */
    void onDealloc(size_t bytes) noexcept {
        totalDeallocations.fetch_add(1, std::memory_order_relaxed);
        liveObjects.fetch_sub(1, std::memory_order_relaxed);
        currentBytes.fetch_sub(bytes, std::memory_order_relaxed);
    }

    /** @brief 重置所有计数器 */
    void reset() noexcept {
        totalAllocations.store(0, std::memory_order_relaxed);
        totalDeallocations.store(0, std::memory_order_relaxed);
        currentBytes.store(0, std::memory_order_relaxed);
        peakBytes.store(0, std::memory_order_relaxed);
        liveObjects.store(0, std::memory_order_relaxed);
    }

    /** @brief 格式化输出统计信息 */
    [[nodiscard]] std::string toString() const {
        std::ostringstream oss;
        oss << "Allocs=" << totalAllocations.load()
            << " Deallocs=" << totalDeallocations.load()
            << " Live=" << liveObjects.load()
            << " Cur=" << currentBytes.load() << "B"
            << " Peak=" << peakBytes.load() << "B";
        return oss.str();
    }
};

// ============================================================================
// 对象池
// ============================================================================

// ============================================================================
// 对象池基类（类型擦除）
// ============================================================================

/**
 * @brief 对象池类型擦除基类
 *
 * MemoryManager 内部使用 unique_ptr<PoolBase> 存储异构对象池。
 */
struct PoolBase {
    virtual ~PoolBase() = default;
};

/**
 * @brief 固定容量对象池（线程安全）
 *
 * 预分配 N 个槽位的原始内存，acquire 时 placement-new 构造，
 * release 时析构并将槽位索引回收至空闲链表。
 *
 * 使用 std::deque 保证指针稳定性：扩容时不失效已有 acquire 返回的指针。
 * 线程安全，适用于频繁创建/销毁小对象的场景。
 *
 * @tparam T 池化管理的数据类型
 */
template<typename T>
class ObjectPool : public PoolBase {
public:
    /**
     * @brief 构造对象池
     * @param initialCapacity 初始槽位数
     * @param allowGrow 池满时是否允许扩容
     * @param stats 可选的分配统计指针
     */
    explicit ObjectPool(size_t initialCapacity = 64, bool allowGrow = true, AllocStats* stats = nullptr)
        : growable_(allowGrow)
        , stats_(stats)
    {
        if (initialCapacity == 0) initialCapacity = 64;
        expand(initialCapacity);
    }

    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& slot : slots_) {
            if (slot.alive) {
                reinterpret_cast<T*>(slot.data)->~T();
                slot.alive = false;
            }
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = delete;
    ObjectPool& operator=(ObjectPool&&) noexcept = delete;

    /**
     * @brief 从池中获取一个对象
     * @tparam Args 构造参数类型
     * @param args 传递给 T 构造函数的参数
     * @return T* 构造好的对象指针，池满且不允许扩容时返回 nullptr
     */
    template<typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) {
        size_t idx;
        bool needsGrow = false;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (!freeIndices_.empty()) {
                idx = freeIndices_.back();
                freeIndices_.pop_back();
            } else if (growable_) {
                idx = slots_.size();
                needsGrow = true;
            } else {
                return nullptr;
            }
        }
        if (needsGrow) {
            std::lock_guard<std::mutex> lock(mtx_);
            idx = slots_.size();
            expand(idx + std::max<size_t>(idx / 2, 16));
            idx = freeIndices_.back();
            freeIndices_.pop_back();
        }
        auto& slot = slots_[idx];
        slot.alive = true;
        T* obj = ::new (slot.data) T(std::forward<Args>(args)...);
        if (stats_) stats_->onAlloc(sizeof(T));
        return obj;
    }

    /**
     * @brief 将对象归还池中
     * @param obj 由 acquire() 返回的指针
     */
    void release(T* obj) {
        if (!obj) return;
        std::lock_guard<std::mutex> lock(mtx_);
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive && reinterpret_cast<void*>(slots_[i].data) == static_cast<void*>(obj)) {
                reinterpret_cast<T*>(slots_[i].data)->~T();
                slots_[i].alive = false;
                freeIndices_.push_back(i);
                if (stats_) stats_->onDealloc(sizeof(T));
                return;
            }
        }
    }

    /** @brief 当前池容量 */
    [[nodiscard]] size_t capacity() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return slots_.size();
    }

    /** @brief 当前活跃对象数 */
    [[nodiscard]] size_t used() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return slots_.size() - freeIndices_.size();
    }

    /** @brief 当前可用槽位数 */
    [[nodiscard]] size_t available() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return freeIndices_.size();
    }

private:
    struct Slot {
        alignas(alignof(T)) unsigned char data[sizeof(T)];
        bool alive = false;
    };

    // 使用 deque 保证扩容时已有指针不失效
    std::deque<Slot> slots_;
    std::vector<size_t> freeIndices_;
    mutable std::mutex mtx_;
    bool growable_;
    AllocStats* stats_;

    void expand(size_t newSize) {
        size_t oldSize = slots_.size();
        if (newSize <= oldSize) return;
        slots_.resize(newSize);
        for (size_t i = oldSize; i < newSize; ++i) {
            freeIndices_.push_back(i);
        }
    }
};

// ============================================================================
// 指针注册表（调试/泄漏检测）
// ============================================================================

/**
 * @brief 弱指针注册表
 *
 * 在 Debug 构建中追踪指针分配/释放位置，关闭时报告泄漏。
 * 仅在 OW_ENABLE_PTR_REGISTRY 定义时生效（Debug 构建自动启用）。
 */
class PtrRegistry {
public:
    /** @brief 指针分配记录 */
    struct Record {
        const void* ptr = nullptr;
        std::string name;
        std::string file;
        int line = 0;
    };

    PtrRegistry() = default;
    ~PtrRegistry() = default;

    PtrRegistry(const PtrRegistry&) = delete;
    PtrRegistry& operator=(const PtrRegistry&) = delete;

    /**
     * @brief 注册一个指针追踪记录
     * @param ptr 要追踪的指针
     * @param name 对象名称或描述
     * @param file 分配发生文件
     * @param line 分配发生行号
     */
    void track(const void* ptr, const std::string& name,
               const std::string& file = "", int line = 0)
    {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mtx_);
        records_[ptr] = {ptr, name, file, line};
    }

    /**
     * @brief 取消追踪一个指针
     */
    void untrack(const void* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mtx_);
        records_.erase(ptr);
    }

    /**
     * @brief 返回当前追踪的指针数量
     */
    [[nodiscard]] size_t trackedCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return records_.size();
    }

    /**
     * @brief 检查泄漏，返回未被 untrack 的指针列表
     */
    [[nodiscard]] std::vector<Record> checkLeaks() {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<Record> leaks;
        leaks.reserve(records_.size());
        for (const auto& [ptr, rec] : records_) {
            leaks.push_back(rec);
        }
        return leaks;
    }

    /** @brief 清空所有追踪记录 */
    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        records_.clear();
    }

private:
    std::unordered_map<const void*, Record> records_;
    mutable std::mutex mtx_;
};

// ============================================================================
// 内存管理器
// ============================================================================

/**
 * @brief 统一内存管理器
 *
 * 整合 AllocStats / ObjectPool / PtrRegistry，提供全局内存管理入口。
 * 可注册为 LifecycleManager 服务，关闭时自动报告泄漏。
 */
class MemoryManager {
public:
    MemoryManager() = default;
    ~MemoryManager() = default;

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // ========== 初始化/清理 ==========

    /**
     * @brief 初始化内存管理器
     * @param enablePtrRegistry 是否启用指针泄漏追踪
     */
    bool init(bool enablePtrRegistry = false);

    /** @brief 清理所有池，报告泄漏 */
    void cleanup();

    // ========== 分配统计 ==========

    /** @brief 获取全局分配统计 */
    [[nodiscard]] AllocStats& getStats() noexcept { return stats_; }
    [[nodiscard]] const AllocStats& getStats() const noexcept { return stats_; }

    // ========== 对象池 ==========

    /**
     * @brief 创建或获取一个命名对象池
     * @tparam T 池化类型
     * @param poolName 池名称（唯一标识）
     * @param initialCapacity 初始容量
     * @param allowGrow 是否允许扩容
     * @return ObjectPool<T>* 池指针，已存在时返回已有池
     */
    template<typename T>
    ObjectPool<T>* getOrCreatePool(const std::string& poolName,
                                   size_t initialCapacity = 64,
                                   bool allowGrow = true)
    {
        std::lock_guard<std::mutex> lock(poolMtx_);
        auto it = objectPools_.find(poolName);
        if (it != objectPools_.end()) {
            return static_cast<ObjectPool<T>*>(it->second.get());
        }
        auto pool = std::make_unique<ObjectPool<T>>(initialCapacity, allowGrow, &stats_);
        auto* ptr = pool.get();
        objectPools_[poolName] = std::move(pool);
        poolNames_.push_back(poolName);
        return ptr;
    }

    /**
     * @brief 销毁一个命名对象池
     */
    void destroyPool(const std::string& poolName) {
        std::lock_guard<std::mutex> lock(poolMtx_);
        objectPools_.erase(poolName);
        auto it = std::find(poolNames_.begin(), poolNames_.end(), poolName);
        if (it != poolNames_.end()) poolNames_.erase(it);
    }

    // ========== 指针注册表 ==========

    /** @brief 获取指针注册表 */
    [[nodiscard]] PtrRegistry* getPtrRegistry() {
        return ptrRegistryEnabled_ ? &ptrRegistry_ : nullptr;
    }

    /** @brief 是否启用了指针注册表 */
    [[nodiscard]] bool isPtrRegistryEnabled() const { return ptrRegistryEnabled_; }

    // ========== 便利宏包装 ==========

    /** @brief 获取当前统计信息的字符串描述 */
    [[nodiscard]] std::string getStatsString() const {
        return stats_.toString();
    }

private:
    AllocStats stats_;
    PtrRegistry ptrRegistry_;
    bool ptrRegistryEnabled_ = false;

    // 对象池存储（类型擦除，PoolBase 确保正确析构）
    std::unordered_map<std::string, std::unique_ptr<PoolBase>> objectPools_;
    std::vector<std::string> poolNames_;
    mutable std::mutex poolMtx_;
};

} // namespace owengine
