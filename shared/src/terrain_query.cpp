#include "terrain_query.h"
#include <cmath>
#include <random>

namespace vgame {

namespace {

static int perm[512];
static std::mt19937 rng(42);
static std::uniform_int_distribution<int> dist(0, 255);
static bool initialized = false;

void initPerm() {
    if (initialized) return;
    for (int i = 0; i < 256; ++i) perm[i] = i;
    for (int i = 255; i > 0; --i) {
        int j = dist(rng) % (i + 1);
        std::swap(perm[i], perm[j]);
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

float TerrainQuery::getHeight(float x, float z) {
    float noiseScale = 0.3f;
    float heightScale = 1.5f;
    float baseHeight = 0.0f;
    
    float noiseX = x * noiseScale;
    float noiseZ = z * noiseScale;
    float height = fbm(noiseX, noiseZ, 4) * heightScale;
    
    return baseHeight + height;
}

} // namespace vgame