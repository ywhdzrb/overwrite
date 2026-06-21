#include "core/memory_manager.hpp"
#include "utils/logger.hpp"

namespace owengine {

bool MemoryManager::init(bool enablePtrRegistry) {
    ptrRegistryEnabled_ = enablePtrRegistry;
    stats_.reset();
    Logger::info("[MemoryManager] 初始化完成"
                 + std::string(enablePtrRegistry ? "，指针追踪已启用" : ""));
    return true;
}

void MemoryManager::cleanup() {
    // 报告指针泄漏
    if (ptrRegistryEnabled_) {
        auto leaks = ptrRegistry_.checkLeaks();
        if (!leaks.empty()) {
            Logger::warning("[MemoryManager] 检测到 " + std::to_string(leaks.size()) + " 个未释放指针：");
            for (const auto& rec : leaks) {
                Logger::warning(std::string("  泄漏: ") + rec.name
                    + (rec.file.empty() ? "" : " @" + rec.file + ":" + std::to_string(rec.line)));
            }
        } else {
            Logger::info("[MemoryManager] 无指针泄漏");
        }
        ptrRegistry_.clear();
    }

    // 销毁所有对象池
    {
        std::lock_guard<std::mutex> lock(poolMtx_);
        for (const auto& name : poolNames_) {
            Logger::info(std::string("[MemoryManager] 销毁池: ") + name);
        }
        objectPools_.clear();
        poolNames_.clear();
    }

    // 输出最终统计
    Logger::info(std::string("[MemoryManager] 最终统计: ") + stats_.toString());
}

} // namespace owengine
