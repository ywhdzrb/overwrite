#pragma once

#include <fstream>
#include <string>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "renderer/grass_system.hpp"
#include "renderer/stone_system.hpp"
#include "renderer/tree_system.hpp"
#include "renderer/terrain_renderer.hpp"
#include "utils/logger.hpp"

namespace owengine {

/// 地形生成配置（与 TerrainParams 字段一一对应）
struct TerrainConfig {
    float continentScale = 0.001f;
    float continentHeight = 12.0f;
    float seaLevel = -2.0f;
    float smoothFreq = 0.008f;
    float roughFreq = 0.025f;
    float plainAmp = 8.0f;
    float mountainAmp = 16.0f;
    float mountainRoughBlend = 0.6f;
    float mountainSeedFreq = 0.0024f;
    float continentRawBase = 0.2f;
    float continentRawSpan = 0.6f;
    float coastBlendStart = 0.2f;
    float coastBlendEnd = 0.6f;
    float oceanDepth = 5.0f;
    float continentBias = 0.3f;
};

/// 水面配置
struct WaterConfig {
    float waveAmplitude = 0.4f;
    float waveFrequency = 0.05f;
    float waveSpeed = 1.2f;
    glm::vec3 color{0.05f, 0.15f, 0.25f};
    float alpha = 0.85f;
};

/// 渲染器配置
struct RendererConfig {
    float targetFPS = 60.0f;          // 目标帧率，用于帧时间同步
    float movementSpeed = 5.0f;       // 玩家移动速度（米/秒）
    float mouseSensitivity = 0.1f;    // 鼠标灵敏度
    int   msaaSamples = 4;            // MSAA 采样数（1/2/4/8/16/32/64）
    float flySpeed = 10.0f;           // 飞行模式上升/下降速度（米/秒）
    glm::vec3 sunDirection{0.25f, 0.55f, 0.50f}; // 天空盒太阳方向（归一化向量）
    bool dayNightCycle = true;                  // 是否启用昼夜循环（false 则固定为 sunDirection）
    bool shadowEnabled = true;                  // 是否启用方向光阴影映射
    uint32_t shadowMapSize = 2048;              // 阴影贴图分辨率
    bool fullscreen = false;                    // 是否以全屏模式启动
};

/// 游戏全局配置，从 JSON 文件加载所有可调参数
struct GameConfig {
    TreeConfig tree;
    StoneConfig stone;
    GrassConfig grass;
    TerrainConfig terrain;
    WaterConfig water;
    RendererConfig renderer;

    /** @brief 预处理 JSON 文本：移除 // 行注释和(斜杠* *斜杠)块注释 */
    static std::string stripJsonComments(const std::string& input) {
        std::string out;
        out.reserve(input.size());
        bool inStr = false, inLine = false, inBlock = false;
        char prev = 0;
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            char n = (i + 1 < input.size()) ? input[i + 1] : 0;
            if (inStr) {
                if (c == '"' && prev != '\\') inStr = false;
                out += c;
            } else if (inLine) {
                if (c == '\n') { inLine = false; out += c; }
            } else if (inBlock) {
                if (c == '*' && n == '/') { inBlock = false; ++i; }
            } else {
                if (c == '"') { inStr = true; out += c; }
                else if (c == '/' && n == '/') { inLine = true; ++i; }
                else if (c == '/' && n == '*') { inBlock = true; ++i; }
                else { out += c; }
            }
            prev = c;
        }
        return out;
    }

    /// 从 JSON 文件加载配置；文件缺失或字段不存在时使用 C++ 默认值
    static GameConfig load(const std::string& path) {
        GameConfig cfg;
        std::ifstream file(path);
        if (!file.is_open()) {
            Logger::warning("未找到配置文件 " + path + "，使用默认参数");
            return cfg;
        }
        try {
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            content = stripJsonComments(content);
            nlohmann::json j = nlohmann::json::parse(content);

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
                cfg.tree.windStrength    = t.value("wind_strength",     cfg.tree.windStrength);
            }

            // 石头参数
            auto& s = j["stone"];
            if (!s.is_null()) {
                cfg.stone.chunkSize       = s.value("chunk_size",        cfg.stone.chunkSize);
                cfg.stone.loadRadius      = s.value("load_radius",       cfg.stone.loadRadius);
                cfg.stone.maxStones       = s.value("max_stones",        cfg.stone.maxStones);
                cfg.stone.minScale        = s.value("min_scale",         cfg.stone.minScale);
                cfg.stone.maxScale        = s.value("max_scale",         cfg.stone.maxScale);
                cfg.stone.density         = s.value("density",           cfg.stone.density);
                cfg.stone.renderDistance  = s.value("render_distance",   cfg.stone.renderDistance);
                cfg.stone.heightThreshold = s.value("height_threshold",  cfg.stone.heightThreshold);
            }

            // 地形参数
            auto& tn = j["terrain"];
            if (!tn.is_null()) {
                cfg.terrain.continentScale     = tn.value("continent_scale",     cfg.terrain.continentScale);
                cfg.terrain.continentHeight    = tn.value("continent_height",    cfg.terrain.continentHeight);
                cfg.terrain.seaLevel           = tn.value("sea_level",           cfg.terrain.seaLevel);
                cfg.terrain.smoothFreq         = tn.value("smooth_freq",         cfg.terrain.smoothFreq);
                cfg.terrain.roughFreq          = tn.value("rough_freq",          cfg.terrain.roughFreq);
                cfg.terrain.plainAmp           = tn.value("plain_amp",           cfg.terrain.plainAmp);
                cfg.terrain.mountainAmp        = tn.value("mountain_amp",        cfg.terrain.mountainAmp);
                cfg.terrain.mountainRoughBlend = tn.value("mountain_rough_blend", cfg.terrain.mountainRoughBlend);
                cfg.terrain.mountainSeedFreq   = tn.value("mountain_seed_freq",  cfg.terrain.mountainSeedFreq);
                cfg.terrain.continentRawBase   = tn.value("continent_raw_base",  cfg.terrain.continentRawBase);
                cfg.terrain.continentRawSpan   = tn.value("continent_raw_span",  cfg.terrain.continentRawSpan);
                cfg.terrain.coastBlendStart    = tn.value("coast_blend_start",   cfg.terrain.coastBlendStart);
                cfg.terrain.coastBlendEnd      = tn.value("coast_blend_end",     cfg.terrain.coastBlendEnd);
                cfg.terrain.oceanDepth         = tn.value("ocean_depth",         cfg.terrain.oceanDepth);
                cfg.terrain.continentBias      = tn.value("continent_bias",      cfg.terrain.continentBias);
            }

            // 水面参数
            auto& w = j["water"];
            if (!w.is_null()) {
                cfg.water.waveAmplitude = w.value("wave_amplitude", cfg.water.waveAmplitude);
                cfg.water.waveFrequency = w.value("wave_frequency", cfg.water.waveFrequency);
                cfg.water.waveSpeed     = w.value("wave_speed",     cfg.water.waveSpeed);
                cfg.water.alpha         = w.value("alpha",          cfg.water.alpha);
                if (w.contains("color_r") && w.contains("color_g") && w.contains("color_b")) {
                    cfg.water.color = glm::vec3(w["color_r"].get<float>(),
                                                w["color_g"].get<float>(),
                                                w["color_b"].get<float>());
                }
            }

            // 渲染器参数
            auto& r = j["renderer"];
            if (!r.is_null()) {
                cfg.renderer.targetFPS        = r.value("target_fps",         cfg.renderer.targetFPS);
                cfg.renderer.movementSpeed    = r.value("movement_speed",     cfg.renderer.movementSpeed);
                cfg.renderer.mouseSensitivity = r.value("mouse_sensitivity",  cfg.renderer.mouseSensitivity);
                cfg.renderer.msaaSamples      = r.value("msaa_samples",       cfg.renderer.msaaSamples);
                cfg.renderer.flySpeed         = r.value("fly_speed",          cfg.renderer.flySpeed);
                // 太阳方向（JSON 数组 [x, y, z]）
                if (r.contains("sun_direction") && r["sun_direction"].is_array()) {
                    auto& sd = r["sun_direction"];
                    cfg.renderer.sunDirection = glm::vec3(sd[0].get<float>(), sd[1].get<float>(), sd[2].get<float>());
                }
                cfg.renderer.dayNightCycle = r.value("day_night_cycle", cfg.renderer.dayNightCycle);
                cfg.renderer.shadowEnabled = r.value("shadow_enabled", cfg.renderer.shadowEnabled);
                cfg.renderer.shadowMapSize = r.value("shadow_map_size", cfg.renderer.shadowMapSize);
                cfg.renderer.fullscreen   = r.value("fullscreen",     cfg.renderer.fullscreen);
            }
        } catch (const std::exception& e) {
            Logger::error("解析配置文件失败: " + std::string(e.what()) + "，使用默认参数");
        }
        return cfg;
    }
};

} // namespace owengine
