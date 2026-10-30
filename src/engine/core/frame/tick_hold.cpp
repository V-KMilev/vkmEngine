#define VKM_LOG_CATEGORY "ASYNC_LOADER"

#include "core/frame/tick_hold.h"

#include <algorithm>

#include "vkm_log/logger.h"

#include "ecs/component/landscape/water.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

void TickHold::gatherNeeded(const Scene& scene, const ResourceManager& resources) {
    m_needed.clear();
    const auto need = [&](MeshHandle mesh) {
        const MeshAsset* asset = resources.tryGet(mesh);
        if (asset && asset->loading) m_needed.push_back(asset->uid());
    };
    scene.forEach<Collider>([&](EntityId, const Collider& collider) {
        for (const ColliderPart& part : collider.parts) {
            if (part.shape == ColliderShape::Mesh) need(part.mesh);
        }
    });
    scene.forEach<Water, Mesh>([&](EntityId, const Water&, const Mesh& mesh) { need(mesh.mesh); });

    std::sort(m_needed.begin(), m_needed.end());
    m_needed.erase(std::unique(m_needed.begin(), m_needed.end()), m_needed.end());
    const auto lost = [&](uint64_t uid) {
        return std::find(m_lost.begin(), m_lost.end(), uid) != m_lost.end();
    };
    m_needed.erase(std::remove_if(m_needed.begin(), m_needed.end(), lost), m_needed.end());
}

bool TickHold::holds(const Scene& scene, const ResourceManager& resources) {
    const Meshes meshes{
        resources.epoch(),
        resources.countOfType<MeshAsset>(),
        resources.revisionOf<MeshAsset>()
    };
    if (meshes == m_still) return false;
    if (meshesLoading(resources) == 0) {
        m_still   = meshes;
        m_waiting = false;
        return false;
    }

    gatherNeeded(scene, resources);
    const size_t left = m_needed.size();
    if (left == 0) {
        m_waiting = false;
        return false;
    }

    // A wait beginning, or a mesh landing, starts the patience again; a mesh starting to decode does not.
    const auto now = std::chrono::steady_clock::now();
    if (!m_waiting || left < m_left) m_since = now;
    m_waiting = true;
    m_left    = left;
    if (now - m_since < m_patience) return true;

    LOG_ERROR(
        "The world's ticks waited %lld ms on %zu mesh(es) with none landing; they run without them",
        static_cast<long long>(m_patience.count()),
        left
    );
    m_lost.insert(m_lost.end(), m_needed.begin(), m_needed.end());
    m_waiting = false;
    return false;
}

} // namespace Vkm::Engine
