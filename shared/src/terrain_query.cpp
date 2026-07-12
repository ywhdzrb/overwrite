#include "terrain_query.hpp"
#include <cmath>
#include <random>
#include <algorithm>

namespace owengine {

namespace {

static int perm[512];
static bool initialized = false;

void initPerm() {
    if (initialized) return;
    static std::mt19937 rng(42);
    static std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 0; i < 256; ++i) perm[i] = i;
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);  // 使用 std::swap
    }
    for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
    initialized = true;
}

} // anonymous namespace

int TerrainQuery::fastFloor(float v) {
    return v > 0 ? static_cast<int>(v) : static_cast<int>(v) - 1;
}

float TerrainQuery::fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float TerrainQuery::lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float TerrainQuery::grad(int h, float x, float y) {
    int hh = h & 15;
    float u = hh < 8 ? x : y;
    float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : 0.0f);
    return ((hh & 1) == 0 ? u : -u) + ((hh & 2) == 0 ? v : -v);
}

float TerrainQuery::perlinNoise(float x, float z) {
    initPerm();

    int xi = fastFloor(x) & 255;
    int zi = fastFloor(z) & 255;

    float xf = x - fastFloor(x);
    float zf = z - fastFloor(z);

    float u = fade(xf);
    float v = fade(zf);

    int aa = perm[perm[xi] + zi];
    int ab = perm[perm[xi] + zi + 1];
    int ba = perm[perm[xi + 1] + zi];
    int bb = perm[perm[xi + 1] + zi + 1];

    float aaGrad = grad(aa, xf, zf);
    float abGrad = grad(ab, xf, zf - 1.0f);
    float baGrad = grad(ba, xf - 1.0f, zf);
    float bbGrad = grad(bb, xf - 1.0f, zf - 1.0f);

    float n0 = lerp(aaGrad, baGrad, u);
    float n1 = lerp(abGrad, bbGrad, u);

    return lerp(n0, n1, v);
}

float TerrainQuery::fbm(float x, float z, int octaves) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += perlinNoise(x * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value / maxValue;
}

// 单源振幅地形生成 — 与 TerrainRenderer::getHeight() 严格一致
float TerrainQuery::getHeight(float x, float z) {
    const float continentScale  = 0.001f;
    const float continentHeight = 12.0f;
    const float seaLevel        = -2.0f;

    // 1. 大陆基底 — 加 0.3 偏置使原点附近为陆地
    float continentRaw = fbm(x * continentScale, z * continentScale, 2) + 0.3f;
    float continentElev = continentRaw * continentHeight;
    float continentFactor = std::max(0.0f, std::min((continentRaw + 0.2f) / 0.6f, 1.0f));

    // 2. 山脉掩码
    float mountainSeed = fbm(x * 0.0024f + 50.0f, z * 0.0024f, 3);
    float mountainMask = std::max(0.0f, std::min((mountainSeed - 0.25f) / 0.5f, 1.0f)) * continentFactor;
    mountainMask = mountainMask * mountainMask * (3.0f - 2.0f * mountainMask);

    // 3. 双频地形噪声
    float smoothTerrain = fbm(x * 0.008f, z * 0.008f, 3);
    float roughTerrain = fbm(x * 0.025f, z * 0.025f, 3);
    float blendTerrain = smoothTerrain * (1.0f - mountainMask * 0.6f) + roughTerrain * (mountainMask * 0.6f);
    float amplitude = 8.0f + mountainMask * 16.0f;

    // 5. 合成
    float height = continentElev + blendTerrain * amplitude;

    // 6. 海岸线平滑过渡
    float oceanFloor = seaLevel - 5.0f * (1.0f - continentFactor);
    float landBlend = std::max(0.0f, std::min((continentFactor - 0.2f) / 0.4f, 1.0f));
    landBlend = landBlend * landBlend * (3.0f - 2.0f * landBlend);
    height = height * landBlend + oceanFloor * (1.0f - landBlend);

    return height;
}

} // namespace owengine
