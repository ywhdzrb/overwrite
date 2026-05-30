#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <cstdio>

// 测试 GameConfig::load 的 JSON 解析逻辑
// 不直接依赖 GameConfig 结构体（避免引入 Vulkan/GLFW 依赖），
// 而是测试 nlohmann_json 的核心解析能力 + 路径常量。
#include "utils/asset_paths.hpp"

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

// ==================== JSON 边界情况测试 ====================

TEST(ConfigTest, JsonNestedObjectAccess) {
    json j = R"({
        "renderer": {
            "target_fps": 144,
            "msaa_samples": 4,
            "sun_direction": [0.5, 0.5, 0.707]
        }
    })"_json;

    auto& r = j["renderer"];
    EXPECT_EQ(r["target_fps"], 144);
    EXPECT_EQ(r["msaa_samples"], 4);
    EXPECT_TRUE(r["sun_direction"].is_array());
    EXPECT_FLOAT_EQ(r["sun_direction"][0], 0.5f);
    EXPECT_FLOAT_EQ(r["sun_direction"][2], 0.707f);
}

TEST(ConfigTest, JsonDefaultValueFallback) {
    // 当字段不存在时 value() 返回默认值
    json j = R"({ "renderer": { "msaa_samples": 8 } })"_json;
    int fps = j["renderer"].value("target_fps", 60);
    EXPECT_EQ(fps, 60);
    // 存在的字段正常读取
    int msaa = j["renderer"].value("msaa_samples", 4);
    EXPECT_EQ(msaa, 8);
}

TEST(ConfigTest, JsonEmptyObject) {
    json j = json::object();
    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.empty());
}

TEST(ConfigTest, JsonArrayOfObjects) {
    json j = R"([
        { "id": "light1", "type": "directional", "intensity": 1.0 },
        { "id": "light2", "type": "point", "intensity": 0.5 }
    ])"_json;

    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 2);
    EXPECT_EQ(j[0]["id"], "light1");
    EXPECT_EQ(j[1]["type"], "point");
    EXPECT_FLOAT_EQ(j[1]["intensity"], 0.5f);
}

TEST(ConfigTest, JsonMixedTypesGraceful) {
    // 混入 null 和 bool 不应导致解析崩溃
    json j = R"({
        "grass": { "density": 0.8, "enabled": true },
        "tree": null,
        "debug": false
    })"_json;

    EXPECT_FLOAT_EQ(j["grass"]["density"], 0.8f);
    EXPECT_TRUE(j["grass"]["enabled"].get<bool>());
    EXPECT_TRUE(j["tree"].is_null());
    EXPECT_FALSE(j["debug"].get<bool>());
}

TEST(ConfigTest, JsonLargeValues) {
    // 测试大数值的整数/浮点解析
    json j = R"({
        "max_blades": 1100000,
        "render_distance": 250.0
    })"_json;

    EXPECT_EQ(j["max_blades"], 1100000);
    EXPECT_FLOAT_EQ(j["render_distance"], 250.0f);
}

TEST(ConfigTest, JsonNegativeValues) {
    json j = R"({
        "height_threshold": -2.0,
        "offset": -0.5
    })"_json;

    EXPECT_FLOAT_EQ(j["height_threshold"], -2.0f);
    EXPECT_FLOAT_EQ(j["offset"], -0.5f);
}
