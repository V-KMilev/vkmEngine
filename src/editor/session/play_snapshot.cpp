#include "session/play_snapshot.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "ecs/scene.h"
#include "io/scene/component_serializer.h"
#include "io/scene/entity_mapping.h"
#include "io/scene/scene_assets.h"
#include "io/scene/scene_components.h"
#include "io/scene/scene_serializer.h"
#include "resource/resource_manager.h"

namespace Vkm::Engine {

namespace {

// Adds to @p out what writes each @p T naming an entity no longer alive back as it stands: the
// document names such an entity as none, and an undo bringing it back would find nothing to re-point.
template <typename T>
void recordDeadReferences(const Scene& scene, std::vector<std::function<void(Scene&)>>& out) {
    scene.forEach<T>([&](EntityId holder, const T& held) {
        std::vector<EntityId> named;
        auto keep = [&](EntityId ref) {
            named.push_back(ref);
            return static_cast<uint32_t>(named.size());
        };
        nlohmann::json saved = ComponentSerializer::save(held, EntityNamer(keep));
        const auto dead = [&](EntityId ref) { return !scene.isAlive(ref); };
        if (std::none_of(named.begin(), named.end(), dead)) return;

        out.push_back([holder, saved = std::move(saved), named = std::move(named)](Scene& restored) {
            T* component = restored.tryGet<T>(holder);
            if (!component) return;
            auto back = [&](uint32_t place) { return named[place - 1]; };
            ComponentSerializer::load(saved, *component, EntityResolver(back));
        });
    });
}

} // namespace

bool PlaySnapshot::capture(const Scene& scene, ResourceManager& resources, bool dirty) {
    m_scene = SceneSerializer::saveToString(scene, resources);
    if (m_scene.empty()) {
        m_assets.clear();
        return false;
    }
    m_assets        = SceneAssets::saveAllAssets(resources).dump();
    m_instanceSlots = Prefab::instanceSlotsOf(scene);
    m_generations   = scene.slotGenerations();
    m_epoch         = scene.epoch();
    m_dirty         = dirty;

    m_deadReferences.clear();
#define VKM_DEAD_NONE(Type, Key)
#define VKM_DEAD_REFERENCES(Type, Key) recordDeadReferences<Type>(scene, m_deadReferences);
    VKM_SCENE_COMPONENTS(VKM_DEAD_NONE, VKM_DEAD_NONE, VKM_DEAD_REFERENCES)
#undef VKM_DEAD_REFERENCES
#undef VKM_DEAD_NONE
    return true;
}

bool PlaySnapshot::restoreInto(Scene& scene, ResourceManager& resources) const {
    if (m_scene.empty()) return false;

    // Before the load, which resolves assets by name.
    if (nlohmann::json assets = nlohmann::json::parse(m_assets, nullptr, false);
        !assets.is_discarded()) {
        SceneAssets::loadAssets(assets, resources, SceneAssets::LoadMode::Reload);
        // Safe for undo: no step taken before an asset existed holds it, and the session's are
        // dropped by this Stop.
        SceneAssets::dropAssetsNotIn(assets, resources);
    }
    if (!SceneSerializer::loadFromString(m_scene, scene, resources, &m_generations)) {
        return false;
    }
    // Only where every entity is back under its id, as the holders are named by theirs.
    if (restoredInPlace(scene)) {
        for (const PutBack& putBack : m_deadReferences) putBack(scene);
    }
    return true;
}

void PlaySnapshot::release() {
    m_scene.clear();
    m_assets.clear();
    m_instanceSlots.clear();
    m_generations.clear();
    m_deadReferences.clear();
    m_epoch      = 0;
    m_dirty      = false;
    m_ejected    = false;
    m_gameCursor = Platform::CursorMode::Normal;
}

void PlaySnapshot::setEjected(bool ejected, Platform::WindowManager& window) {
    if (!held() || ejected == m_ejected) return;
    m_ejected = ejected;
    if (ejected) {
        m_gameCursor = window.cursorMode();
        window.setCursorMode(Platform::CursorMode::Normal);
    } else {
        window.setCursorMode(m_gameCursor);
    }
}

void PlaySnapshot::holdCursorFree(Platform::WindowManager& window) {
    if (!m_ejected || window.cursorMode() == Platform::CursorMode::Normal) return;
    m_gameCursor = window.cursorMode();
    window.setCursorMode(Platform::CursorMode::Normal);
}

} // namespace Vkm::Engine
