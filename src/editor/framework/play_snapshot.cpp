#include "framework/play_snapshot.h"

#include <nlohmann/json.hpp>

#include "logger.h"

#include "ecs/scene.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

bool PlaySnapshot::capture(const Scene& scene, ResourceManager& resources,
                           bool dirty, unsigned long long history) {
    m_scene = SceneSerializer::saveToString(scene, resources);
    if (m_scene.empty()) {
        m_assets.clear();
        return false;
    }
    m_assets  = AssetSerializer::saveAllAssets(resources).dump();
    m_dirty   = dirty;
    m_history = history;
    return true;
}

bool PlaySnapshot::restoreInto(Scene& scene, ResourceManager& resources) const {
    if (m_scene.empty()) return false;

    // Ahead of the load, which resolves names: an asset the scene names has to
    // exist before the scene asks for it.
    if (nlohmann::json assets = nlohmann::json::parse(m_assets, nullptr, false);
        !assets.is_discarded()) {
        AssetSerializer::loadAssets(assets, resources, AssetSerializer::LoadMode::Reload);
    }
    return SceneSerializer::loadFromString(m_scene, scene, resources);
}

void PlaySnapshot::release() {
    m_scene.clear();
    m_assets.clear();
    m_dirty   = false;
    m_history = 0;
}

} // namespace Vkm::Engine
