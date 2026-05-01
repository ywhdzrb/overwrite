#pragma once

#include <string>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include "utils/logger.h"

// TreeConfig 定义在 tree_system.h 中，本文件直接使用
#include "renderer/tree_system.h"
// GrassConfig 定义在 grass_system.h 中
#include "renderer/grass_system.h"

namespace owengine {

/// 渲染器配置
struct RendererConfig {
    float targetFPS = 60.0f;          // 目标帧率，用于帧时间同步
    float movementSpeed = 5.0f;       // 玩家移动速度（米/秒）
    float mouseSensitivity = 0.1f;    // 鼠标灵敏度
    int   msaaSamples = 4;            // MSAA 采样数（1/2/4/8/16/32/64）
    float flySpeed = 10.0f;           // 飞行模式上升/下降速度（米/秒）
};

/// 游戏全局配置，从 JSON 文件加载所有可调参数
struct GameConfig {
    TreeConfig tree;
    GrassConfig grass;
    RendererConfig renderer;

    /// 从 JSON 文件加载配置；文件缺失或字段不存在时使用 C++ 默认值
    static GameConfig load(const std::string& path) {
        GameConfig cfg;
        std::ifstream file(path);
        if (!file.is_open()) {
            Logger::warning("未找到配置文件 " + path + "，使用默认参数");
            return cfg;
        }
        try {
            nlohmann::json j;
            file >> j;

            // 草丛参数
            auto& g = j["grass"];
            if (!g.is_null()) {
                cfg.grass.chunkSize       = g.value("chunk_size",        cfg.grass.chunkSize);
                cfg.grass.loadRadius      = g.value("load_radius",       cfg.grass.loadRadius);
                cfg.grass.maxBlades       = g.value("max_blades",        cfg.grass.maxBlades);
                cfg.grass.density         = g.value("density",           cfg.grass.density);
                cfg.grass.renderDistance  = g.value("render_distance",   cfg.grass.renderDistance);
                cfg.grass.bladeHeightMin  = g.value("blade_height_min",  cfg.grass.bladeHeightMin);
                cfg.grass.bladeHeightMax  = g.value("blade_height_max",  cfg.grass.bladeHeightMax);
                cfg.grass.segmentsPerBlade = g.value("segments_per_blade", cfg.grass.segmentsPerBlade);
                cfg.grass.windStrength    = g.value("wind_strength",     cfg.grass.windStrength);
                cfg.grass.playerRadius    = g.value("player_radius",     cfg.grass.playerRadius);
                cfg.grass.playerForce     = g.value("player_force",      cfg.grass.playerForce);
            }

            // 树木参数
            auto& t = j["tree"];
            if (!t.is_null()) {
                cfg.tree.chunkSize       = t.value("chunk_size",        cfg.tree.chunkSize);
                cfg.tree.loadRadius      = t.value("load_radius",       cfg.tree.loadRadius);
                cfg.tree.maxTotal        = t.value("max_total",         cfg.tree.maxTotal);
                cfg.tree.minScale        = t.value("min_scale",         cfg.tree.minScale);
                cfg.tree.maxScale        = t.value("max_scale",         cfg.tree.maxScale);
                cfg.tree.density         = t.value("density",           cfg.tree.density);
                cfg.tree.renderDistance  = t.value("render_distance",   cfg.tree.renderDistance);
                cfg.tree.heightThreshold = t.value("height_threshold",  cfg.tree.heightThreshold);
            }

            // 渲染器参数
            auto& r = j["renderer"];
            if (!r.is_null()) {
                cfg.renderer.targetFPS        = r.value("target_fps",         cfg.renderer.targetFPS);
                cfg.renderer.movementSpeed    = r.value("movement_speed",     cfg.renderer.movementSpeed);
                cfg.renderer.mouseSensitivity = r.value("mouse_sensitivity",  cfg.renderer.mouseSensitivity);
                cfg.renderer.msaaSamples      = r.value("msaa_samples",       cfg.renderer.msaaSamples);
                cfg.renderer.flySpeed         = r.value("fly_speed",          cfg.renderer.flySpeed);
            }
        } catch (const std::exception& e) {
            Logger::error("解析配置文件失败: " + std::string(e.what()) + "，使用默认参数");
        }
        return cfg;
    }
};

} // namespace owengine
