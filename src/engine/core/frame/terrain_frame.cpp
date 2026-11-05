#include "core/frame/terrain_frame.h"

namespace Vkm::Engine::TerrainChunks {

uint32_t levelAt(float distance, float chunkWidth) {
    uint32_t level = 0;
    float    reach = REACH * chunkWidth;
    while (level + 1 < LEVELS && distance > reach) {
        ++level;
        reach *= 2.0f;
    }
    return level;
}

} // namespace Vkm::Engine::TerrainChunks
