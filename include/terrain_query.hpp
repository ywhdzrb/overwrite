#pragma once

/**
 * @file terrain_query.hpp
 * @brief 三层地形查询（共享）— 服务端与客户端统一的程序化地形生成
 *
 * 归属模块：shared
 * 核心职责：提供与 TerrainRenderer 完全一致的三层地形高度查询，
 *           保证联机时服务端和客户端对同一世界坐标返回相同高度。
 * 关键设计：所有噪声参数、算法实现与 terrain_renderer.cpp 严格一致，
 *           种子 42、Perm 表初始化方式完全相同。
 */

#include <glm/glm.hpp>

namespace owengine {

/**
 * @brief 三层地形查询静态工具类
 *
 * 与 TerrainRenderer::getHeight() 使用完全相同的算法和参数，
 * 确保服务端和客户端地形高度完全一致。
 */
class TerrainQuery {
public:
    static float getHeight(float x, float z);

private:
    static float perlinNoise(float x, float z);
    static float fbm(float x, float z, int octaves);

    static int fastFloor(float x);
    static float fade(float t);
    static float lerp(float a, float b, float t);
    static float grad(int h, float x, float y);
};

} // namespace owengine
