/**
 * @file test_memory_manager.cpp
 * @brief 内存管理器单元测试（ObjectPool / AllocStats / PtrRegistry / MemoryManager）
 *
 * 测试对象池的构造/析构、自动扩容、槽位复用、线程安全，
 * 以及统计系统和指针注册表的正确性。
 */
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>

#include "core/memory_manager.hpp"

using namespace owengine;

// ==================== 构造/析构 ====================

TEST(MemoryManagerTest, ObjectPoolBasicAcquireRelease) {
    ObjectPool<int> pool(4, false);
    EXPECT_EQ(pool.capacity(), 4);
    EXPECT_EQ(pool.used(), 0);

    int* a = pool.acquire(42);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(*a, 42);
    EXPECT_EQ(pool.used(), 1);

    int* b = pool.acquire(100);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(*b, 100);
    EXPECT_EQ(pool.used(), 2);

    pool.release(a);
    EXPECT_EQ(pool.used(), 1);

    pool.release(b);
    EXPECT_EQ(pool.used(), 0);
}

// ==================== 自动扩容 ====================

TEST(MemoryManagerTest, ObjectPoolAutoGrow) {
    ObjectPool<int> pool(4, true);
    std::vector<int*> ptrs;
    for (int i = 0; i < 100; i++) {
        int* p = pool.acquire(i);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(*p, i);
        ptrs.push_back(p);
    }
    EXPECT_EQ(pool.used(), 100);
    EXPECT_GE(pool.capacity(), 100);

    for (auto p : ptrs) {
        pool.release(p);
    }
    EXPECT_EQ(pool.used(), 0);
}

// ==================== 不可扩容时返回 nullptr ====================

TEST(MemoryManagerTest, ObjectPoolNoGrowReturnsNull) {
    ObjectPool<int> pool(2, false);
    EXPECT_NE(pool.acquire(1), nullptr);
    EXPECT_NE(pool.acquire(2), nullptr);
    EXPECT_EQ(pool.acquire(3), nullptr);
}

// ==================== 释放后槽位复用 ====================

TEST(MemoryManagerTest, ObjectPoolSlotReuse) {
    ObjectPool<int> pool(4, false);
    int* a = pool.acquire(10);
    int* b = pool.acquire(20);
    pool.release(a);
    int* c = pool.acquire(30);
    // c 应该复用 a 的槽位（LIFO 空闲链表）
    EXPECT_EQ(c, a);
    EXPECT_EQ(*c, 30);
    pool.release(b);
    pool.release(c);
}

// ==================== 空指针释放安全 ====================

TEST(MemoryManagerTest, ObjectPoolNullRelease) {
    ObjectPool<int> pool(4, false);
    // 释放空指针不应崩溃
    pool.release(nullptr);
    int* a = pool.acquire(1);
    pool.release(a);
    pool.release(nullptr);
    SUCCEED();
}

// ==================== 对象析构正确 ====================

namespace {
struct DtorTracker {
    static std::atomic<int> aliveCount;
    DtorTracker() { aliveCount++; }
    ~DtorTracker() { aliveCount--; }
    DtorTracker(const DtorTracker&) = delete;
    DtorTracker& operator=(const DtorTracker&) = delete;
};
std::atomic<int> DtorTracker::aliveCount{0};
}

TEST(MemoryManagerTest, ObjectPoolDestructorRuns) {
    DtorTracker::aliveCount.store(0);
    {
        ObjectPool<DtorTracker> pool(4, false);
        auto* _t1 = pool.acquire();
        auto* _t2 = pool.acquire();
        (void)_t1; (void)_t2;
        EXPECT_EQ(DtorTracker::aliveCount, 2);
    }
    EXPECT_EQ(DtorTracker::aliveCount, 0);
}

// ==================== 线程安全 ====================

TEST(MemoryManagerTest, ObjectPoolThreadSafety) {
    constexpr int NUM_THREADS = 4;
    constexpr int OPS_PER_THREAD = 1000;
    ObjectPool<int> pool(64, true);

    std::vector<std::thread> threads;
    std::atomic<uint64_t> totalAcquired{0};

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&]() {
            std::vector<int*> local;
            for (int i = 0; i < OPS_PER_THREAD; i++) {
                int* p = pool.acquire(i);
                if (p) {
                    local.push_back(p);
                    totalAcquired.fetch_add(1, std::memory_order_relaxed);
                }
            }
            for (auto p : local) {
                pool.release(p);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(pool.used(), 0);
    EXPECT_GT(totalAcquired.load(), 0);
}

// ==================== AllocStats ====================

TEST(MemoryManagerTest, AllocStatsBasic) {
    AllocStats stats;
    EXPECT_EQ(stats.liveObjects.load(), 0);
    EXPECT_EQ(stats.currentBytes.load(), 0);

    stats.onAlloc(64);
    EXPECT_EQ(stats.totalAllocations.load(), 1);
    EXPECT_EQ(stats.liveObjects.load(), 1);
    EXPECT_EQ(stats.currentBytes.load(), 64);
    EXPECT_EQ(stats.peakBytes.load(), 64);

    stats.onAlloc(32);
    EXPECT_EQ(stats.liveObjects.load(), 2);
    EXPECT_EQ(stats.currentBytes.load(), 96);
    EXPECT_EQ(stats.peakBytes.load(), 96);

    stats.onDealloc(32);
    EXPECT_EQ(stats.liveObjects.load(), 1);
    EXPECT_EQ(stats.currentBytes.load(), 64);
    EXPECT_EQ(stats.peakBytes.load(), 96);  // 峰值不变

    stats.onDealloc(64);
    EXPECT_EQ(stats.currentBytes.load(), 0);

    std::string s = stats.toString();
    EXPECT_FALSE(s.empty());
    EXPECT_NE(s.find("Allocs=2"), std::string::npos);
}

// ==================== PtrRegistry ====================

TEST(MemoryManagerTest, PtrRegistryTrackUntrack) {
    PtrRegistry reg;
    int x = 42;
    reg.track(&x, "test_int", __FILE__, __LINE__);
    EXPECT_EQ(reg.trackedCount(), 1);

    reg.untrack(&x);
    EXPECT_EQ(reg.trackedCount(), 0);
}

TEST(MemoryManagerTest, PtrRegistryCheckLeaks) {
    PtrRegistry reg;
    int a = 1, b = 2;
    reg.track(&a, "a");
    reg.track(&b, "b");
    reg.untrack(&a);

    auto leaks = reg.checkLeaks();
    ASSERT_EQ(leaks.size(), 1);
    EXPECT_EQ(leaks[0].name, "b");
}

TEST(MemoryManagerTest, PtrRegistryNullSafety) {
    PtrRegistry reg;
    reg.track(nullptr, "null");  // 应忽略
    EXPECT_EQ(reg.trackedCount(), 0);
    reg.untrack(nullptr);  // 应安全
    SUCCEED();
}

// ==================== MemoryManager ====================

TEST(MemoryManagerTest, MemoryManagerGetOrCreatePool) {
    MemoryManager mm;
    ASSERT_TRUE(mm.init(false));

    auto* pool = mm.getOrCreatePool<int>("ints", 16, true);
    ASSERT_NE(pool, nullptr);
    EXPECT_EQ(pool->capacity(), 16);

    // 同名池应返回同一实例
    auto* pool2 = mm.getOrCreatePool<int>("ints", 32, false);
    EXPECT_EQ(pool, pool2);

    // 不同名池应创建新实例
    auto* pool3 = mm.getOrCreatePool<float>("floats");
    ASSERT_NE(pool3, nullptr);
    EXPECT_NE(static_cast<void*>(pool3), static_cast<void*>(pool));

    mm.destroyPool("ints");
    mm.destroyPool("floats");

    auto statsStr = mm.getStatsString();
    EXPECT_FALSE(statsStr.empty());

    mm.cleanup();
}

// ==================== ObjectPool with Stats ====================

TEST(MemoryManagerTest, ObjectPoolWithStats) {
    AllocStats stats;
    {
        ObjectPool<int> pool(4, true, &stats);
        int* a = pool.acquire(1);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(stats.liveObjects.load(), 1);
        EXPECT_GT(stats.currentBytes.load(), 0);

        pool.release(a);
        EXPECT_EQ(stats.liveObjects.load(), 0);
        EXPECT_EQ(stats.currentBytes.load(), 0);
    }
}
