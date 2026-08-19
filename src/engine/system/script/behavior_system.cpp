#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior_system.h"

#include <filesystem>

#include <exception>
#include <utility>

#include "logger.h"

#include "core/clock.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "platform/window/window_manager.h"
#include "core/event/event_bus.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {

// Build the "TypeName / hook" label used in behavior error reports. Called only
// on throw, so it never allocates on the per-hook hot path.
std::string hookLabel(const Behavior& behavior, const char* hookName) {
    return std::string(behavior.typeName()) + " / " + hookName;
}

// Run a hook body under the catch net, reporting any throw. Returns true if it
// threw, leaving the caller to decide the consequence (guard disables the
// behavior; teardown just logs and moves on).
template<typename Fn>
bool runGuarded(Behavior& behavior, const char* hookName, Fn&& fn) {
    try {
        fn();
        return false;
    } catch (const std::exception& e) {
        reportError("Behavior", hookLabel(behavior, hookName), e.what());
    } catch (...) {
        // A non-std throw would otherwise escape into the system loop and crash
        // the engine (ThreadPool already guards with catch(...)); contain it here.
        reportError("Behavior", hookLabel(behavior, hookName), "non-std exception");
    }
    return true;
}

} // namespace

template<typename Fn>
void BehaviorSystem::guard(Behavior& behavior, const char* hookName, Fn&& fn) {
    if (runGuarded(behavior, hookName, std::forward<Fn>(fn))) {
        behavior.m_disabled = true;
    }
}

void BehaviorSystem::ensureStarted(Behavior& behavior, EntityId entity) {
    if (behavior.m_started) return;
    behavior.bindContext(entity, m_context);
    guard(behavior, "onStart", [&] {
        behavior.onStart();
        behavior.m_started = true;
    });
}

void BehaviorSystem::tickBehaviors(FrameContext& ctx, float dt, const char* hookName,
                                   void (Behavior::*hook)(float)) {
    Scene& scene = ctx.scene;
    auto* storage = scene.storage<ScriptComponent>();
    if (!storage) return;

    // Snapshot who to tick before running anything. A hook is free to spawn an
    // entity and script it, which grows this very storage; iterating it live
    // would hand the loop a reference into a buffer that has since moved.
    m_tickList.clear();
    m_tickList.reserve(storage->size());
    storage->forEach([&](uint32_t entityIdx, ScriptComponent&) {
        m_tickList.push_back(scene.entityAt(entityIdx));
    });

    for (const EntityId id : m_tickList) {
        // Re-resolved every step: the entity may have been destroyed by an
        // earlier hook, and the storage may have moved since the snapshot.
        if (!scene.isAlive(id) || !scene.has<ScriptComponent>(id)) continue;

        const size_t behaviorCount = scene.get<ScriptComponent>(id).behaviors.size();
        for (size_t i = 0; i < behaviorCount; ++i) {
            if (!scene.isAlive(id) || !scene.has<ScriptComponent>(id)) break;
            ScriptComponent& sc = scene.get<ScriptComponent>(id);
            if (i >= sc.behaviors.size()) break;

            auto& behavior = sc.behaviors[i];
            if (!behavior || behavior->m_disabled) continue;
            ensureStarted(*behavior, id);
            if (behavior->m_disabled) continue;  // onStart threw
            Behavior* b = behavior.get();
            guard(*b, hookName, [&] { (b->*hook)(dt); });
        }
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
    // onDestroy mirrors onStart: never started, never destroyed. Unlike guard(),
    // a throw here is logged but doesn't disable - the behavior is being torn
    // down anyway.
    if (behavior.m_started) {
        runGuarded(behavior, "onDestroy", [&] { behavior.onDestroy(); });
    }
    behavior.clearSubscriptions();
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

void BehaviorSystem::drainPendingSceneLoad(FrameContext& ctx) {
    if (m_pendingSceneLoad.empty()) return;

    // Take the request before loading: the load runs behaviors' onDestroy, and
    // one of those asking for another scene must queue for the next pass rather
    // than be swallowed by the clear below.
    std::string path;
    path.swap(m_pendingSceneLoad);

    // Give the outgoing scene's behaviors their onDestroy while they are still
    // alive and their module still holds the code. The load below destroys them
    // as part of the swap, which would otherwise run their destructors without
    // the hook they are documented to get.
    endSession(ctx.scene);

    const std::filesystem::path scenePath = ProjectPaths::projectRoot() / path;
    if (SceneSerializer::load(ctx.scene, ctx.resources, scenePath.string())) {
        LOG_INFO("Loaded scene '%s' on request", path.c_str());
    } else {
        // The load is transactional, so a failure leaves the current scene
        // standing rather than an empty world. Its behaviors have already had
        // onDestroy from the endSession above and will start again on the next
        // tick, which is the closest thing to a recovery there is.
        LOG_ERROR("Requested scene '%s' failed to load; staying in the current one",
                  scenePath.string().c_str());
    }
}

void BehaviorSystem::init(FrameContext& ctx) {
    // Complete the capability bundle (pendingDestroy was wired at
    // construction): every behavior binds a pointer to it, so its fields must
    // all be session-stable - which everything on the FrameContext service
    // block is.
    m_context.scene     = &ctx.scene;
    m_context.resources = &ctx.resources;
    m_context.window    = &ctx.window;
    m_context.events    = &ctx.events;
    m_context.input     = &ctx.input;

    // onDestroy for any entity-deletion path: register as a Scene observer, so
    // Scene fires onEntityDestroyed from destroyEntity (raw or via
    // destroyHierarchy) while staying script-agnostic.
    ctx.scene.addObserver(this);

    // Physics overlaps -> behavior hooks. Collect here; dispatch in update()
    // once behaviors are started and with valid context.
    m_context.events->subscribe<CollisionEvent>([this](const CollisionEvent& e) { m_collisions.push_back(e); });
    m_context.events->subscribe<TriggerEvent>([this](const TriggerEvent& e) { m_triggers.push_back(e); });
}

void BehaviorSystem::onEntityDestroyed(EntityId entity) {
    if (m_context.scene) destroyEntityBehaviors(*m_context.scene, entity);
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
    drainPendingSceneLoad(ctx);
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
    if (!m_context.scene) return;
    endSession(*m_context.scene);            // onDestroy + drop subscriptions while the bus lives
    m_context.scene->removeObserver(this);   // avoid a callback into this dying system
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

} // namespace Vkm::Engine
