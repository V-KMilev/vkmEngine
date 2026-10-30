#define VKM_LOG_CATEGORY "ASYNC_LOADER"

#include "core/frame/arrival_hold.h"

#include <cstdint>

#include "vkm_log/logger.h"

#include "ecs/scene.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

size_t assetsLoading(const ResourceManager& resources) {
    size_t count = meshesLoading(resources);
    resources.forEachOfType<TextureAsset>([&](TextureHandle, const TextureAsset& texture) {
        count += texture.loading;
    });
    return count;
}

size_t meshesLoading(const ResourceManager& resources) {
    size_t count = 0;
    resources.forEachOfType<MeshAsset>([&](MeshHandle, const MeshAsset& mesh) { count += mesh.loading; });
    return count;
}

bool ArrivalHold::holds(const Scene& scene, const ResourceManager& resources, bool skyDecoding) {
    const auto now = std::chrono::steady_clock::now();
    if (!m_seen || scene.epoch() != m_epoch) {
        m_seen    = true;
        m_epoch   = scene.epoch();
        m_waiting = true;
        m_left    = SIZE_MAX;
        // What the backend decodes for this world, it says once it has drawn it.
        return true;
    }
    if (!m_waiting) return false;

    // A landing restarts the patience; a load starting to decode does not.
    const size_t left = assetsLoading(resources) + (skyDecoding ? 1 : 0);
    if (left < m_left) m_since = now;
    m_left = left;
    if (left == 0) {
        m_waiting = false;
    } else if (now - m_since >= m_patience) {
        LOG_ERROR(
            "The world waited %lld ms on %zu asset(s) with none landing; its first bakes begin without them",
            static_cast<long long>(m_patience.count()),
            left
        );
        m_waiting = false;
    }
    return m_waiting;
}

} // namespace Vkm::Engine
