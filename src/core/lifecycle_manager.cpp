#include "core/lifecycle_manager.hpp"
#include "utils/logger.hpp"

#include <algorithm>
#include <queue>
#include <set>

namespace owengine {

LifecycleManager::~LifecycleManager() {
    if (!initOrder_.empty()) {
        shutdown();
    }
}

void LifecycleManager::registerService(
    const std::string& name,
    std::vector<std::string> dependencies,
    std::function<bool()> initFn,
    std::function<void()> cleanupFn)
{
    ServiceRecord record;
    record.name = name;
    record.dependencies = std::move(dependencies);
    record.initFn = std::move(initFn);
    record.cleanupFn = std::move(cleanupFn);
    record.initialized = false;
    services_[name] = std::move(record);
}

std::vector<std::string> LifecycleManager::topologicalSort() {
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> adjList;
    for (const auto& [name, _] : services_) {
        inDegree[name] = 0;
        adjList[name] = {};
    }
    for (const auto& [name, record] : services_) {
        for (const auto& dep : record.dependencies) {
            auto it = services_.find(dep);
            if (it != services_.end()) {
                adjList[dep].push_back(name);
                inDegree[name]++;
            } else {
                Logger::warning(std::string("[LifecycleManager] '") + name + "' 依赖 '" + dep + "' 未注册");
            }
        }
    }
    std::queue<std::string> q;
    for (const auto& [name, deg] : inDegree) {
        if (deg == 0) q.push(name);
    }
    std::vector<std::string> result;
    result.reserve(services_.size());
    while (!q.empty()) {
        auto node = q.front(); q.pop();
        result.push_back(node);
        for (const auto& neighbor : adjList[node]) {
            if (--inDegree[neighbor] == 0) q.push(neighbor);
        }
    }
    if (result.size() != services_.size()) {
        size_t missing = services_.size() - result.size();
        Logger::warning(std::string("[LifecycleManager] 循环依赖 ") + std::to_string(missing) + " 个节点追加到末尾");
        std::set<std::string> visited(result.begin(), result.end());
        for (const auto& [name, _] : services_) {
            if (visited.find(name) == visited.end()) result.push_back(name);
        }
    }
    return result;
}

bool LifecycleManager::initialize() {
    if (services_.empty()) {
        Logger::info("[LifecycleManager] 无服务注册");
        return true;
    }
    auto order = topologicalSort();
    Logger::info(std::string("[LifecycleManager] 初始化 ") + std::to_string(order.size()) + " 个服务");
    for (const auto& name : order) {
        auto it = services_.find(name);
        if (it == services_.end()) continue;
        Logger::info(std::string("[LifecycleManager] 初始化: ") + name);
        bool ok = false;
        try {
            ok = it->second.initFn();
        } catch (const std::exception& e) {
            Logger::error(std::string("[LifecycleManager] '") + name + "' 异常: " + e.what());
        } catch (...) {
            Logger::error(std::string("[LifecycleManager] '") + name + "' 未知异常");
        }
        if (!ok) {
            Logger::error(std::string("[LifecycleManager] '") + name + "' 失败，回滚中");
            for (auto rit = initOrder_.rbegin(); rit != initOrder_.rend(); ++rit) {
                auto rec = services_.find(*rit);
                if (rec != services_.end() && rec->second.initialized) {
                    Logger::info(std::string("[LifecycleManager] 回滚: ") + *rit);
                    try { rec->second.cleanupFn(); } catch (...) {}
                    rec->second.initialized = false;
                }
            }
            initOrder_.clear();
            return false;
        }
        it->second.initialized = true;
        initOrder_.push_back(name);
    }
    Logger::info(std::string("[LifecycleManager] 全部 ") + std::to_string(initOrder_.size()) + " 个服务就绪");
    return true;
}

void LifecycleManager::shutdown() {
    if (initOrder_.empty()) return;
    Logger::info(std::string("[LifecycleManager] 关闭 ") + std::to_string(initOrder_.size()) + " 个服务");
    for (auto rit = initOrder_.rbegin(); rit != initOrder_.rend(); ++rit) {
        auto it = services_.find(*rit);
        if (it != services_.end() && it->second.initialized) {
            Logger::info(std::string("[LifecycleManager] 关闭: ") + *rit);
            try {
                it->second.cleanupFn();
            } catch (const std::exception& e) {
                Logger::error(std::string("[LifecycleManager] '") + *rit + "' 关闭异常: " + e.what());
            } catch (...) {
                Logger::error(std::string("[LifecycleManager] '") + *rit + "' 关闭未知异常");
            }
            it->second.initialized = false;
        }
    }
    initOrder_.clear();
    Logger::info("[LifecycleManager] 所有服务已关闭");
}

bool LifecycleManager::isInitialized(const std::string& name) const {
    auto it = services_.find(name);
    return it != services_.end() && it->second.initialized;
}

std::vector<std::string> LifecycleManager::getRegisteredServices() const {
    std::vector<std::string> names;
    names.reserve(services_.size());
    for (const auto& [name, _] : services_) {
        names.push_back(name);
    }
    return names;
}

} // namespace owengine
