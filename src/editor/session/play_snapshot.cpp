#include "session/play_snapshot.h"

#include <nlohmann/json.hpp>

#include "ecs/scene.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

bool PlaySnapshot::capture(const Scene& scene, ResourceManager& resources, bool dirty) {
    m_scene = SceneSerializer::saveToString(scene, resources);
    if (m_scene.empty()) {
        m_assets.clear();
        return false;
    }
    m_assets        = AssetSerializer::saveAllAssets(resources).dump();
    m_instanceSlots = Prefab::instanceSlotsOf(scene);
    m_dirty         = dirty;
    return true;
}

bool PlaySnapshot::restoreInto(Scene& scene, ResourceManager& resources) const {
    if (m_scene.empty()) return false;

    // Before the load, which resolves assets by name.
    if (nlohmann::json assets = nlohmann::json::parse(m_assets, nullptr, false);
        !assets.is_discarded()) {
        AssetSerializer::loadAssets(assets, resources, AssetSerializer::LoadMode::Reload);
        // Reload leaves assets the document does not name alone, so drop them. Safe
        // for undo: no step taken before an asset existed holds it, and steps
        // taken during the session are dropped by this Stop.
        AssetSerializer::dropAssetsNotIn(assets, resources);
    }
    return SceneSerializer::loadFromString(m_scene, scene, resources, &m_instanceSlots);
}

void PlaySnapshot::release() {
    m_scene.clear();
    m_assets.clear();
    m_instanceSlots.clear();
    m_dirty      = false;
    m_ejected    = false;
    m_gameCursor = CursorMode::Normal;
}

void PlaySnapshot::setEjected(bool ejected, WindowManager& window) {
    if (!held() || ejected == m_ejected) return;
    m_ejected = ejected;
    if (ejected) {
        m_gameCursor = window.cursorMode();
        window.setCursorMode(CursorMode::Normal);
    } else {
        window.setCursorMode(m_gameCursor);
    }
}

void PlaySnapshot::holdCursorFree(WindowManager& window) {
    if (!m_ejected || window.cursorMode() == CursorMode::Normal) return;
    m_gameCursor = window.cursorMode();
    window.setCursorMode(CursorMode::Normal);
}

} // namespace Vkm::Engine
