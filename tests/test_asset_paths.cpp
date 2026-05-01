#include <gtest/gtest.h>
#include <cstring>

// 仅包含资产路径常量，不依赖 Vulkan/GLFW
#include "utils/asset_paths.h"

using namespace owengine::AssetPaths;

// 验证所有路径常量非空且有预期前缀
TEST(AssetPathsTest, ShaderPathsNonEmpty) {
    EXPECT_GT(std::strlen(MAIN_VERT_SHADER), 0u);
    EXPECT_GT(std::strlen(MAIN_FRAG_SHADER), 0u);
    EXPECT_GT(std::strlen(SKYBOX_VERT_SHADER), 0u);
    EXPECT_GT(std::strlen(GRASS_VERT_SHADER), 0u);
    EXPECT_GT(std::strlen(FSR1_EASU_SHADER), 0u);
}

TEST(AssetPathsTest, AssetPathsNonEmpty) {
    EXPECT_GT(std::strlen(PLAYER_IDLE_MODEL), 0u);
    EXPECT_GT(std::strlen(PLAYER_WALK_MODEL), 0u);
    EXPECT_GT(std::strlen(TREE_MODEL), 0u);
    EXPECT_GT(std::strlen(SKYBOX_TEXTURE), 0u);
}

TEST(AssetPathsTest, ConfigPathsNonEmpty) {
    EXPECT_GT(std::strlen(SCENE_CONFIG), 0u);
    EXPECT_GT(std::strlen(GAME_CONFIG), 0u);
}

TEST(AssetPathsTest, ShaderPathPrefix) {
    // 着色器路径应以 "shaders/" 开头
    EXPECT_EQ(std::strncmp(MAIN_VERT_SHADER, "shaders/", 8), 0);
    EXPECT_EQ(std::strncmp(MAIN_FRAG_SHADER, "shaders/", 8), 0);
}

TEST(AssetPathsTest, AssetPathPrefix) {
    // 模型/纹理路径应以 "assets/" 开头
    EXPECT_EQ(std::strncmp(PLAYER_IDLE_MODEL, "assets/", 7), 0);
    EXPECT_EQ(std::strncmp(TREE_MODEL, "assets/", 7), 0);
    EXPECT_EQ(std::strncmp(SKYBOX_TEXTURE, "assets/", 7), 0);
}
