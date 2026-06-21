#pragma once

/**
 * @file lifecycle_manager.hpp
 * @brief 基于依赖图的启动/关闭生命周期管理器
 *
 * 归属模块：core
 * 核心职责：
 *   1. 按依赖拓扑顺序初始化服务（依赖先启动）
 *   2. 按依赖逆序关闭服务（确保依赖链安全）
 *   3. 初始化失败时自动回滚已启动的服务
 * 依赖关系：Logger（仅日志输出）
 * 关键设计：使用 Kahn 算法做拓扑排序；初始化/关闭全程 try-catch 保护
 */

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>

namespace owengine {

/**
 * @brief 生命周期管理器
 *
 * 启动顺序：以 Kahn 拓扑排序确定，依赖先行。
 * 关闭顺序：拓扑排序的严格逆序，确保没有服务在它的依赖之前被销毁。
 * 异常安全：每个 initFn 由 try-catch 包裹，失败时自动回滚已初始化服务。
 *
 * 使用示例：
 * @code
 *   LifecycleManager lm;
 *   lm.registerService("Vulkan", {},   []{ return initVulkan(); }, cleanupVulkan);
 *   lm.registerService("Swapchain", {"Vulkan"}, []{ return initSwapchain(); }, cleanupSwapchain);
 *   if (!lm.initialize()) { // 处理失败
 *   }
 *   lm.shutdown(); // Swapchain → Vulkan
 * @endcode
 */
class LifecycleManager {
public:
    LifecycleManager() = default;
    ~LifecycleManager();

    LifecycleManager(const LifecycleManager&) = delete;
    LifecycleManager& operator=(const LifecycleManager&) = delete;

    // ========== 注册 ==========

    /**
     * @brief 注册一个服务
     * @param name 服务唯一标识符
     * @param dependencies 依赖的服务名称列表（这些服务必须先初始化）
     * @param initFn 初始化回调，返回 true 表示成功
     * @param cleanupFn 清理回调（禁止抛异常）
     */
    void registerService(
        const std::string& name,
        std::vector<std::string> dependencies,
        std::function<bool()> initFn,
        std::function<void()> cleanupFn
    );

    // ========== 生命周期 ==========

    /**
     * @brief 按依赖拓扑顺序初始化所有已注册的服务
     * @return true 全部初始化成功
     * @note 失败时自动回滚已初始化的服务，进程状态回到初始化前
     */
    bool initialize();

    /**
     * @brief 按依赖逆序清理所有已初始化的服务
     * @note 可在任意时刻安全调用（即使从未 initialize 过）
     */
    void shutdown();

    // ========== 查询 ==========

    /**
     * @brief 检查指定服务是否已初始化成功
     */
    bool isInitialized(const std::string& name) const;

    /**
     * @brief 获取当前已初始化的服务数量
     */
    size_t initializedCount() const { return initOrder_.size(); }

    /**
     * @brief 获取所有已注册的服务名称列表
     */
    std::vector<std::string> getRegisteredServices() const;

    /**
     * @brief 获取初始化顺序（拓扑序）
     */
    const std::vector<std::string>& getInitOrder() const { return initOrder_; }

private:
    /**
     * @brief 服务记录
     */
    struct ServiceRecord {
        std::string name;
        std::vector<std::string> dependencies;
        std::function<bool()> initFn;
        std::function<void()> cleanupFn;
        bool initialized = false;
    };

    /**
     * @brief Kahn 算法拓扑排序
     * @return 按依赖顺序排列的服务名称列表
     * @note 检测到循环依赖时将剩余节点追加到末尾并告警
     */
    std::vector<std::string> topologicalSort();

    // 所有注册的服务
    std::unordered_map<std::string, ServiceRecord> services_;

    // 当前已初始化的服务（按初始化顺序排列）
    std::vector<std::string> initOrder_;
};

} // namespace owengine
