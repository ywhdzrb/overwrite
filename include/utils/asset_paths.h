#pragma once

// ============================================================
// 资源路径常量
// 所有硬编码的资产、着色器、配置文件路径集中于此，
// 便于统一管理和修改。
// ============================================================

namespace owengine::AssetPaths {

// ==================== 基础目录 ====================

/** @brief 着色器 SPIR-V 目录（相对于运行时工作目录 build/） */
inline constexpr const char* SHADER_DIR = "shaders/";

/** @brief 模型/纹理等资产目录 */
inline constexpr const char* ASSET_DIR = "assets/";

/** @brief 纹理子目录 */
inline constexpr const char* TEXTURE_DIR = "assets/textures/";

/** @brief 模型子目录 */
inline constexpr const char* MODEL_DIR = "assets/models/";

/** @brief 配置文件目录 */
inline constexpr const char* CONFIG_DIR = "config/";

// ==================== 着色器文件 ====================

/** @brief 主渲染管线顶点着色器 */
inline constexpr const char* MAIN_VERT_SHADER = "shaders/shader.vert.spv";
/** @brief 主渲染管线片段着色器 */
inline constexpr const char* MAIN_FRAG_SHADER = "shaders/shader.frag.spv";

/** @brief 天空盒顶点着色器 */
inline constexpr const char* SKYBOX_VERT_SHADER = "shaders/skybox.vert.spv";
/** @brief 天空盒片段着色器 */
inline constexpr const char* SKYBOX_FRAG_SHADER = "shaders/skybox.frag.spv";

/** @brief 草地顶点着色器 */
inline constexpr const char* GRASS_VERT_SHADER = "shaders/grass.vert.spv";
/** @brief 草地片段着色器 */
inline constexpr const char* GRASS_FRAG_SHADER = "shaders/grass.frag.spv";

/** @brief FSR1 EASU 计算着色器 */
inline constexpr const char* FSR1_EASU_SHADER = "shaders/fsr1_easu.comp.spv";

// ==================== 纹理文件 ====================

/** @brief 天空盒十字布局纹理 */
inline constexpr const char* SKYBOX_TEXTURE = "assets/textures/skybox.jpg";

// ==================== 模型文件 ====================

/** @brief 玩家空闲模型 */
inline constexpr const char* PLAYER_IDLE_MODEL = "assets/models/player.glb";
/** @brief 玩家行走模型 */
inline constexpr const char* PLAYER_WALK_MODEL = "assets/models/player_walk.glb";
/** @brief 树木模型 */
inline constexpr const char* TREE_MODEL = "assets/models/tree.glb";
/** @brief 石头模型 */
inline constexpr const char* STONE_MODEL = "assets/models/stone.gltf";

// ==================== 配置文件 ====================

/** @brief 场景配置文件（光源、模型布局） */
inline constexpr const char* SCENE_CONFIG = "config/scene.json";
/** @brief 游戏全局配置文件 */
inline constexpr const char* GAME_CONFIG = "config/game_config.json";

} // namespace owengine::AssetPaths
