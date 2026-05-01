#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <cstdio>

// 测试 GameConfig::load 的 JSON 解析逻辑
// 不直接依赖 GameConfig 结构体（避免引入 Vulkan/GLFW 依赖），
// 而是测试 nlohmann_json 的核心解析能力 + 路径常量。
#include "utils/asset_paths.h"

using json = nlohmann::json;

TEST(ConfigTest, JsonBasicParse) {
    json j = R"({
        "grass": { "density": 0.8, "max_blades": 50000 },
        "tree":  { "density": 0.02, "max_total": 2000 },
        "renderer": { "target_fps": 144, "msaa_samples": 4 }
    })"_json;

    EXPECT_FLOAT_EQ(j["grass"]["density"], 0.8f);
    EXPECT_EQ(j["tree"]["max_total"], 2000);
    EXPECT_EQ(j["renderer"]["target_fps"], 144);
}

TEST(ConfigTest, JsonMissingFieldGetsDefault) {
    json j = R"({ "renderer": {} })"_json;

    auto& r = j["renderer"];
    int fps  = r.value("target_fps", 60);
    int msaa = r.value("msaa_samples", 4);
    EXPECT_EQ(fps, 60);
    EXPECT_EQ(msaa, 4);
}

TEST(ConfigTest, JsonUnknownFieldIgnored) {
    // 未知字段不应导致解析失败
    json j = R"({
        "grass": { "density": 0.5 },
        "unknown_field": "should_not_crash"
    })"_json;

    EXPECT_NO_THROW({
        float d = j.value("grass", json::object()).value("density", 0.0f);
        EXPECT_FLOAT_EQ(d, 0.5f);
    });
}

TEST(ConfigTest, AssetPathConstantsValid) {
    // 验证配置文件路径是非空的（运行时加载由 GameConfig::load 负责）
    EXPECT_NE(owengine::AssetPaths::GAME_CONFIG[0], '\0');
    EXPECT_NE(owengine::AssetPaths::SCENE_CONFIG[0], '\0');
}
