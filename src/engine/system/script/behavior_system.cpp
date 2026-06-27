#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior_system.h"

#include <exception>
#include <utility>

#include "logger.h"

#include "core/clock.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "platform/window/window_manager.h"
#include "system/event/event_system.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

namespace Engine {

template<typename Fn>
void BehaviorSystem::guard(Behavior& behavior, const char* hookName, Fn&& fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        reportError("Behavior", std::string(behavior.typeName()) + " / " + hookName, e.what());
        behavior.m_disabled = true;
    } catch (...) {
        // A non-std throw would otherwise escape into the system loop and crash
        // the engine (ThreadPool already guards with catch(...)); contain it here.
        reportError("Behavior", std::string(behavior.typeName()) + " / " + hookName, "non-std exception");
        behavior.m_disabled = true;
    }
}

void BehaviorSystem::ensureStarted(Behavior& behavior, EntityId entity, FrameContext& ctx) {
    if (behavior.m_started) return;
    behavior.bindContext(entity, ctx.scene, ctx.resources,
                         ctx.window.getInputHandle(), m_events, m_pendingDestroy);
    guard(behavior, "onStart", [&] {
        behavior.onStart();
        behavior.m_started = true;
    });
}

void BehaviorSystem::tickBehaviors(FrameContext& ctx, float dt, const char* hookName,
                                   void (Behavior::*hook)(float)) {
    Scene& scene = ctx.scene;
    if (auto* storage = scene.storage<ScriptComponent>()) {
        storage->forEach([&](uint32_t entityIdx, ScriptComponent& sc) {
            const EntityId id{entityIdx, scene.generationOf(entityIdx)};
            for (auto& behavior : sc.behaviors) {
                if (!behavior || behavior->m_disabled) continue;
                ensureStarted(*behavior, id, ctx);
                if (behavior->m_disabled) continue;  // onStart threw
                Behavior* b = behavior.get();
                guard(*b, hookName, [&] { (b->*hook)(dt); });
            }
        });
    }
}

void BehaviorSystem::dispatchEntityHook(Scene& scene, EntityId target, EntityId other,
                                        const char* hookName, void (Behavior::*hook)(EntityId)) {
    if (!scene.isAlive(target) || !scene.has<ScriptComponent>(target)) return;
    ScriptComponent& sc = scene.get<ScriptComponent>(target);
    for (auto& behavior : sc.behaviors) {
        if (!behavior || !behavior->m_started || behavior->m_disabled) continue;
        Behavior* b = behavior.get();
        guard(*b, hookName, [&] { (b->*hook)(other); });
    }
}

void BehaviorSystem::fireDestroy(Behavior& behavior) {
    if (behavior.m_started) {  // onDestroy mirrors onStart - never started, never destroyed
        try {
            behavior.onDestroy();
        } catch (const std::exception& e) {
            reportError("Behavior", std::string(behavior.typeName()) + " / onDestroy", e.what());
        } catch (...) {
            reportError("Behavior", std::string(behavior.typeName()) + " / onDestroy", "non-std exception");
        }
    }
    behavior.clearSubscriptions();  // drop listeners while the EventSystem is alive
}

void BehaviorSystem::drainPendingDestroy(Scene& scene) {
    if (m_pendingDestroy.empty()) return;
    // Swap out so destroys requested from within onDestroy land in fresh storage
    // and drain next pass instead of invalidating this iteration.
    std::vector<EntityId> pending;
    pending.swap(m_pendingDestroy);
    for (EntityId entity : pending) {
        if (scene.isAlive(entity)) HierarchyOperations::destroyHierarchy(scene, entity);
    }
}

void BehaviorSystem::init(FrameContext& ctx) {
    m_scene = &ctx.scene;

    // onDestroy for any entity-deletion path: register as a Scene observer, so
    // Scene fires onEntityDestroyed from destroyEntity (raw or via
    // destroyHierarchy) while staying script-agnostic.
    ctx.scene.addObserver(this);

    // Physics overlaps -> behavior hooks. Collect here; dispatch in update()
    // once behaviors are started and with valid context.
    m_events.subscribe<CollisionEvent>([this](const CollisionEvent& e) { m_collisions.push_back(e); });
    m_events.subscribe<TriggerEvent>([this](const TriggerEvent& e) { m_triggers.push_back(e); });
}

void BehaviorSystem::onEntityDestroyed(EntityId entity) {
    if (m_scene) destroyEntityBehaviors(*m_scene, entity);
}

void BehaviorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem");

    // No simulation time elapsed (paused / not stepping): scripts don't tick.
    // Drop any queued physics events so they don't pile up across a pause.
    if (ctx.clock.getSimDelta() <= 0.0f) {
        m_collisions.clear();
        m_triggers.clear();
        return;
    }

    Scene& scene = ctx.scene;

    tickBehaviors(ctx, ctx.clock.getSimDelta(), "onUpdate", &Behavior::onUpdate);

    // Dispatch collisions/triggers gathered since last frame. Swap to locals so
    // a handler that emits a synchronous event can't mutate the list mid-walk.
    std::vector<CollisionEvent> collisions;
    collisions.swap(m_collisions);
    for (const CollisionEvent& e : collisions) {
        dispatchEntityHook(scene, e.a, e.b, "onCollision", &Behavior::onCollision);
        dispatchEntityHook(scene, e.b, e.a, "onCollision", &Behavior::onCollision);
    }
    std::vector<TriggerEvent> triggers;
    triggers.swap(m_triggers);
    for (const TriggerEvent& e : triggers) {
        dispatchEntityHook(scene, e.trigger, e.other, "onTrigger", &Behavior::onTrigger);
    }

    drainPendingDestroy(scene);
}

void BehaviorSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem::fixed");

    // The accumulator that drives fixedUpdate is fed from simDeltaTime, so this
    // only runs while playing (or per queued step) - no explicit pause gate.
    // fixedUpdate runs before update each frame; onStart fires here if this is
    // the instance's first tick.
    tickBehaviors(ctx, ctx.clock.getFixedStep(), "onFixedUpdate", &Behavior::onFixedUpdate);

    drainPendingDestroy(ctx.scene);
}

void BehaviorSystem::shutdown() {
    if (!m_scene) return;
    endSession(*m_scene);            // onDestroy + drop subscriptions while the bus lives
    m_scene->removeObserver(this);  // avoid a callback into this dying system
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
