#pragma once

// 标准库
#include <string>
#include <sstream>
#include <stdexcept>

// 第三方库
#include <nlohmann/json.hpp>

// 项目内部头文件
#include "utils/logger.hpp"

namespace owengine {

/**
 * @brief 全局 JSON 配置管理器
 *
 * 支持多文件加载合并、点分路径访问、模板类型安全读写。
 * 外部模块可通过 get/set 方法读写任意嵌套层级，无需定义冗余结构体。
 *
 * 生命周期：全局单例或局部实例均可，由使用者管理生命周期。
 * 线程安全：当前实现非线程安全，外部需加锁。
 */
class ConfigManager {
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // 禁止拷贝
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // 允许移动
    ConfigManager(ConfigManager&&) noexcept = default;
    ConfigManager& operator=(ConfigManager&&) noexcept = default;

    /**
     * @brief 加载 JSON 配置文件（合并到已有配置）
     * @param path JSON 文件路径
     * @return 是否成功加载
     *
     * 合并策略：nlohmann::json::merge_patch，顶级字段按 key 合并，
     * 同名字段覆盖，不同名字段保留。
     */
    [[nodiscard]] bool loadFile(const std::string& path);

    /**
     * @brief 检查点分路径是否存在
     * @param key 点分路径，如 "renderer.sun_direction"
     * @return 路径是否存在且非 null
     */
    [[nodiscard]] bool has(const std::string& key) const;

    /**
     * @brief 获取点分路径对应的值
     * @tparam T 目标类型（必须能被 nlohmann::json::get<T>() 转换）
     * @param key 点分路径
     * @param default_value 路径不存在或类型不匹配时的默认值
     * @return 配置值或默认值
     */
    template<typename T>
    T get(const std::string& key, const T& default_value = T{}) const {
        const auto* node = resolvePath(root_, key);
        if (node) {
            try {
                return node->get<T>();
            } catch (const nlohmann::json::exception&) {
                // 类型转换失败，返回默认值
            }
        }
        return default_value;
    }

    /**
     * @brief 设置点分路径对应的值
     * @tparam T 值类型
     * @param key 点分路径，中间路径自动创建为 object
     * @param value 要设置的值
     */
    template<typename T>
    void set(const std::string& key, const T& value) {
        std::istringstream ss(key);
        std::string segment;
        nlohmann::json* current = &root_;
        while (std::getline(ss, segment, '.')) {
            if (ss.peek() == EOF) {
                // 最后一段：写入值
                (*current)[segment] = value;
            } else {
                // 中间路径：确保是 object
                if (!current->contains(segment)) {
                    (*current)[segment] = nlohmann::json::object();
                }
                current = &(*current)[segment];
            }
        }
    }

    /**
     * @brief 将当前配置保存到文件
     * @param path 目标文件路径
     * @param indent JSON 缩进空格数
     * @return 是否成功保存
     */
    [[nodiscard]] bool saveToFile(const std::string& path, int indent = 4) const;

    /// @brief 清空所有已加载配置
    void clear() { root_.clear(); }

    /// @brief 获取底层 JSON 对象（只读）
    [[nodiscard]] const nlohmann::json& root() const { return root_; }

    /// @brief 获取底层 JSON 对象（读写）
    nlohmann::json& root() { return root_; }

private:
    nlohmann::json root_ = nlohmann::json::object(); ///< 合并后的配置根节点

    /**
     * @brief 解析点分路径，返回可变节点指针
     * @param j JSON 根节点
     * @param key 点分路径
     * @return 节点指针，路径不存在返回 nullptr
     */
    static nlohmann::json* resolvePath(nlohmann::json& j, const std::string& key);

    /**
     * @brief 解析点分路径，返回只读节点指针
     * @param j JSON 根节点
     * @param key 点分路径
     * @return 节点指针，路径不存在返回 nullptr
     */
    static const nlohmann::json* resolvePath(const nlohmann::json& j, const std::string& key);
};

} // namespace owengine
