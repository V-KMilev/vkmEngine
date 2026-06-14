#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior_system.h"

#include <exception>

#include "logger.h"

#include "debug/behavior_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

namespace Engine {

void BehaviorSystem::ensureStarted(Behavior& behavior, EntityId entity, Scene& scene, ResourceManager& resources) {
    if (behavior.m_started) return;
    try {
        behavior.bindContext(entity, scene, resources);
        behavior.onStart();
        behavior.m_started = true;
    } catch (const std::exception& e) {
        BehaviorErrorLog::get().push(behavior.typeName(), "onStart", e.what());
        LOG_ERROR("Behavior '%s' threw in onStart - disabling instance: %s", behavior.typeName(), e.what());
        behavior.m_disabled = true;
    }
}

void BehaviorSystem::invoke(Behavior& behavior, const char* hookName, void (Behavior::*hook)(float), float dt) {
    try {
        (behavior.*hook)(dt);
    } catch (const std::exception& e) {
        BehaviorErrorLog::get().push(behavior.typeName(), hookName, e.what());
        LOG_ERROR("Behavior '%s' threw in %s - disabling instance: %s", behavior.typeName(), hookName, e.what());
        behavior.m_disabled = true;
    }
}

void BehaviorSystem::fireDestroy(Behavior& behavior) {
    if (!behavior.m_started) return;  // onDestroy mirrors onStart - never started, never destroyed
    try {
        behavior.onDestroy();
    } catch (const std::exception& e) {
        BehaviorErrorLog::get().push(behavior.typeName(), "onDestroy", e.what());
        LOG_ERROR("Behavior '%s' threw in onDestroy: %s", behavior.typeName(), e.what());
    }
}

void BehaviorSystem::init(FrameContext& ctx) {
    m_scene = &ctx.scene;
}

void BehaviorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem");

    // No simulation time elapsed (paused / not stepping): scripts don't tick,
    // so play/pause/step/Stop all fall out of the SimulationClock for free.
    if (ctx.simDeltaTime <= 0.0f) return;

    Scene& scene = ctx.scene;
    auto* storage = scene.storage<ScriptComponent>();
    if (!storage) return;

    const float dt = ctx.simDeltaTime;
    storage->forEach([&](uint32_t entityIdx, ScriptComponent& sc) {
        const EntityId id{entityIdx, scene.generationOf(entityIdx)};
        for (auto& behavior : sc.behaviors) {
            if (!behavior || behavior->m_disabled) continue;
            ensureStarted(*behavior, id, scene, ctx.resources);
            if (behavior->m_disabled) continue;  // onStart threw
            invoke(*behavior, "onUpdate", &Behavior::onUpdate, dt);
        }
    });
}

void BehaviorSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem::fixed");

    // The accumulator that drives fixedUpdate is fed from simDeltaTime, so this
    // only runs while playing (or per queued step) - no explicit pause gate.
    Scene& scene = ctx.scene;
    auto* storage = scene.storage<ScriptComponent>();
    if (!storage) return;

    const float dt = ctx.fixedDeltaTime;
    storage->forEach([&](uint32_t entityIdx, ScriptComponent& sc) {
        const EntityId id{entityIdx, scene.generationOf(entityIdx)};
        for (auto& behavior : sc.behaviors) {
            if (!behavior || behavior->m_disabled) continue;
            // fixedUpdate runs before update each frame; onStart fires here if
            // this is the instance's first tick.
            ensureStarted(*behavior, id, scene, ctx.resources);
            if (behavior->m_disabled) continue;
            invoke(*behavior, "onFixedUpdate", &Behavior::onFixedUpdate, dt);
        }
    });
}

void BehaviorSystem::shutdown() {
    if (m_scene) endSession(*m_scene);
}

void BehaviorSystem::endSession(Scene& scene) {
    auto* storage = scene.storage<ScriptComponent>();
    if (!storage) return;
    storage->forEach([&](uint32_t, ScriptComponent& sc) {
        for (auto& behavior : sc.behaviors) {
            if (!behavior) continue;
            fireDestroy(*behavior);
            behavior->m_started  = false;
            behavior->m_disabled = false;
        }
    });
}

void BehaviorSystem::destroyEntityBehaviors(Scene& scene, EntityId entity) {
    if (!scene.has<ScriptComponent>(entity)) return;
    ScriptComponent& sc = scene.get<ScriptComponent>(entity);
    for (auto& behavior : sc.behaviors) {
        if (behavior) fireDestroy(*behavior);
    }
}

} // namespace Engine
