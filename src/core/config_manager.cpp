// 配置管理器实现
// 提供 JSON 配置文件的加载/合并/点分路径访问/保存功能。
#include "core/config_manager.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace owengine {

bool ConfigManager::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::warning("无法打开配置文件: " + path);
        return false;
    }

    try {
        // 先读为字符串，移除 // 和 /* */ 注释后再解析
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        // 简单注释剥离器：处理 // 行注释和 /* 块注释
        {
            std::string clean;
            clean.reserve(content.size());
            bool inStr = false, inLine = false, inBlock = false;
            char prev = 0;
            for (size_t i = 0; i < content.size(); ++i) {
                char c = content[i];
                char n = (i + 1 < content.size()) ? content[i + 1] : 0;
                if (inStr) {
                    if (c == '"' && prev != '\\') inStr = false;
                    clean += c;
                } else if (inLine) {
                    if (c == '\n') { inLine = false; clean += c; }
                } else if (inBlock) {
                    if (c == '*' && n == '/') { inBlock = false; ++i; }
                } else {
                    if (c == '"') { inStr = true; clean += c; }
                    else if (c == '/' && n == '/') { inLine = true; ++i; }
                    else if (c == '/' && n == '*') { inBlock = true; ++i; }
                    else { clean += c; }
                }
                prev = c;
            }
            content = std::move(clean);
        }

        nlohmann::json j = nlohmann::json::parse(content);

        // 首次加载：直接赋值
        // 后续加载：merge_patch 递归合并，顶级字段覆盖/新增
        if (root_.is_null()) {
            root_ = std::move(j);
        } else {
            root_.merge_patch(j);
        }

        Logger::info("配置文件加载成功: " + path);
        return true;

    } catch (const nlohmann::json::exception& e) {
        Logger::error(std::string("JSON 解析错误 (") + path + "): " + e.what());
        return false;
    }
}

bool ConfigManager::has(const std::string& key) const {
    const auto* node = resolvePath(root_, key);
    return node != nullptr && !node->is_null();
}

bool ConfigManager::saveToFile(const std::string& path, int indent) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::error("无法保存配置文件: " + path);
        return false;
    }

    try {
        file << root_.dump(indent);
        Logger::info("配置保存成功: " + path);
        return true;
    } catch (const nlohmann::json::exception& e) {
        Logger::error(std::string("配置序列化失败 (") + path + "): " + e.what());
        return false;
    }
}

nlohmann::json* ConfigManager::resolvePath(nlohmann::json& j, const std::string& key) {
    if (key.empty() || j.is_null()) {
        return nullptr;
    }

    nlohmann::json* current = &j;
    std::istringstream ss(key);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (!current->is_object() || !current->contains(segment)) {
            return nullptr;
        }
        current = &(*current)[segment];
    }

    return current;
}

const nlohmann::json* ConfigManager::resolvePath(const nlohmann::json& j, const std::string& key) {
    if (key.empty() || j.is_null()) {
        return nullptr;
    }

    const nlohmann::json* current = &j;
    std::istringstream ss(key);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        if (!current->is_object() || !current->contains(segment)) {
            return nullptr;
        }
        current = &(*current)[segment];
    }

    return current;
}

} // namespace owengine
