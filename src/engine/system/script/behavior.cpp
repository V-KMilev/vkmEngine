#include "system/script/behavior.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "core/math/rotation.h"
#include "ecs/component/core/transform.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "net/wire/protocol.h"
#include "resource/resource_manager.h"
#include "system/audio/audio_events.h"
#include "system/ocean/ocean_data.h"
#include "system/script/script_component.h"
#include "system/water/water_data.h"

namespace Vkm::Engine {

const Behavior* findBehaviorNamed(const Scene& scene, EntityId entity, std::string_view typeName) {
    const ScriptComponent* script = scene.tryGet<ScriptComponent>(entity);
    if (!script) return nullptr;

    for (const std::unique_ptr<Behavior>& behavior : script->behaviors) {
        if (behavior && typeName == behavior->typeName()) return behavior.get();
    }
    return nullptr;
}

Behavior* findBehaviorNamed(Scene& scene, EntityId entity, std::string_view typeName) {
    // The scene's own, so the behavior is as writable as the scene handed in.
    return const_cast<Behavior*>(findBehaviorNamed(std::as_const(scene), entity, typeName));
}

EntityId findEntityWithBehaviorNamed(Scene& scene, std::string_view typeName) {
    EntityId found{};
    scene.forEach<ScriptComponent>([&](EntityId entity, ScriptComponent&) {
        if (found && found.slot() < entity.slot()) return;
        if (findBehaviorNamed(scene, entity, typeName)) found = entity;
    });
    return found;
}

namespace {

/// What forEachBehaviorNamed meets, kept per thread for its capacity.
thread_local std::vector<std::pair<EntityId, Behavior*>> t_carrying;

} // namespace

void forEachBehaviorNamed(
    Scene& scene,
    std::string_view typeName,
    const std::function<void(EntityId, Behavior&)>& fn
) {
    // Borrowed by swap, so a walk fn starts takes a fresh list instead of this one.
    std::vector<std::pair<EntityId, Behavior*>> carrying;
    carrying.swap(t_carrying);
    scene.forEach<ScriptComponent>([&](EntityId entity, ScriptComponent&) {
        Behavior* behavior = findBehaviorNamed(scene, entity, typeName);
        if (behavior) carrying.emplace_back(entity, behavior);
    });
    // The storage's order is not one two ends of a session share; the slots' is.
    std::sort(carrying.begin(), carrying.end(), [](const auto& a, const auto& b) {
        return a.first.slot() < b.first.slot();
    });
    for (const auto& [entity, behavior] : carrying) fn(entity, *behavior);
    carrying.clear();
    t_carrying.swap(carrying);
}

const OceanData* Behavior::ocean() const {
    const OceanData* sea = m_ctx->ocean;
    return sea && sea->present ? sea : nullptr;
}

const WaterData& Behavior::water() const {
    static const WaterData NONE;
    return m_ctx->water ? *m_ctx->water : NONE;
}

glm::vec3 Behavior::worldPosition() const {
    if (!tryGet<Transform>()) return glm::vec3(0.0f);
    return glm::vec3(HierarchyOperations::computeWorldMatrix(*m_ctx->scene, m_entity)[3]);
}

glm::quat Behavior::worldRotation() const {
    if (!tryGet<Transform>()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return Math::worldRotationOf(HierarchyOperations::computeWorldMatrix(*m_ctx->scene, m_entity));
}

bool Behavior::isAuthority() const {
    return Vkm::Engine::isAuthority(m_ctx->net->role());
}

double Behavior::worldTime() const {
    return m_ctx->net->worldTime(*m_ctx->clock);
}

EntityId Behavior::instantiate(const std::string& prefab, const Transform& at) {
    return Prefab::instantiate(*m_ctx->scene, *m_ctx->resources, prefab, at);
}

EntityId Behavior::instantiateShared(const std::string& prefab, const Transform& at) {
    return m_ctx->net->spawn(*m_ctx->scene, *m_ctx->resources, prefab, at);
}

void Behavior::sendSound(
    const AssetRef<AudioClipAsset>& clip,
    const glm::vec3& at,
    float volume,
    EntityId from
) {
    if (isReplaying()) return;  // a replay re-runs a moment already presented
    const AudioClipHandle sound = m_ctx->resources->find(clip);
    if (!sound) return;
    PlaySoundEvent request = PlaySoundEvent::at(sound, at, volume);
    request.from           = from;
    m_ctx->events->emit(request);
}

ScreenView Behavior::screen() const {
    if (!m_ctx->window || !m_ctx->chrome) return {};
    return ScreenView::of(*m_ctx->scene, *m_ctx->window, *m_ctx->chrome);
}

std::optional<Math::Ray> Behavior::pointerRay() const {
    return screen().rayThrough(m_ctx->input->pointer());
}

} // namespace Vkm::Engine
