#ifndef TERRAIN_QUERY_H
#define TERRAIN_QUERY_H

#include <glm/glm.hpp>

namespace owengine {

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

#endif // TERRAIN_QUERY_H