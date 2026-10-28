#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior_system.h"

#include <algorithm>
#include <filesystem>
#include <type_traits>
#include <utility>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "net/net_session.h"
#include "platform/window/window_manager.h"
#include "core/event/event_bus.h"
#include "io/project_paths.h"
#include "io/scene/scene_fingerprint.h"
#include "io/scene/scene_serializer.h"
#include "system/loading/loading_events.h"
#include "system/loading/loading_frame.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"

namespace Vkm::Engine {

namespace {

// What tickBehaviors does with a behavior that has not started yet.
constexpr bool START_IF_NEEDED = true;
constexpr bool NEVER_START     = false;

/**
 * @brief Invoke @p fn on each of @p entity's behaviors, re-resolving as it goes.
 *
 * A hook may destroy the entity or add or remove a ScriptComponent, moving or freeing
 * the vector being walked, so each step re-checks the entity and re-fetches the
 * component, ending early if the list shrank.
 *
 * @param scene Scene holding the entity.
 * @param entity Entity whose behaviors to visit; a dead one visits none.
 * @param fn Called with each non-null behavior, in order.
 */
template<typename Fn>
void forEachBehaviorOf(Scene& scene, EntityId entity, Fn&& fn) {
    const ScriptComponent* first = scene.tryGet<ScriptComponent>(entity);
    if (!first) return;

    const size_t behaviorCount = first->behaviors.size();
    for (size_t i = 0; i < behaviorCount; ++i) {
        ScriptComponent* sc = scene.tryGet<ScriptComponent>(entity);
        if (!sc || i >= sc->behaviors.size()) break;

        if (Behavior* behavior = sc->behaviors[i].get()) fn(*behavior);
    }
}

/// The hook a contact phase is delivered to, and its name for an error report.
template<typename Arg>
struct ContactHook {
    const char* name;
    void (Behavior::*call)(Arg);
};

ContactHook<const Collision&> collisionHook(ContactPhase phase) {
    switch (phase) {
        case ContactPhase::Began:  return {"onCollisionEnter", &Behavior::onCollisionEnter};
        case ContactPhase::Stayed: return {"onCollisionStay",  &Behavior::onCollisionStay};
        case ContactPhase::Ended:  break;
    }
    return {"onCollisionExit", &Behavior::onCollisionExit};
}

ContactHook<EntityId> triggerHook(ContactPhase phase) {
    switch (phase) {
        case ContactPhase::Began:  return {"onTriggerEnter", &Behavior::onTriggerEnter};
        case ContactPhase::Stayed: return {"onTriggerStay",  &Behavior::onTriggerStay};
        case ContactPhase::Ended:  break;
    }
    return {"onTriggerExit", &Behavior::onTriggerExit};
}

} // namespace

template<typename Fn>
void BehaviorSystem::guard(Behavior& behavior, const char* hookName, Fn&& fn) {
    if (behavior.runGuarded(hookName, std::forward<Fn>(fn))) {
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

void BehaviorSystem::tickBehaviors(
    FrameContext& ctx,
    float dt,
    const char* hookName,
    void (Behavior::*hook)(float),
    bool startIfNeeded
) {
    Scene& scene = ctx.scene;
    auto* storage = scene.storage<ScriptComponent>();
    if (!storage) return;

    // A replay re-runs only what this end simulates; a live tick runs every behavior,
    // since an unsimulated one still draws and plays sounds.
    const bool replaying = ctx.net.replaying();

    m_tickList.clear();
    m_tickList.reserve(storage->size());
    storage->forEach([&](uint32_t entityIdx, ScriptComponent&) {
        const EntityId entity = scene.entityAt(entityIdx);
        if (replaying && !ctx.net.simulates(ctx.scene, entity)) return;
        m_tickList.push_back(entity);
    });

    // By slot, so the order depends on the world, not the SparseSet's add/destroy
    // history; hooks touch each other's state, so order is part of the answer.
    std::sort(
        m_tickList.begin(),
        m_tickList.end(),
        [](EntityId a, EntityId b) { return a.slot() < b.slot(); }
    );

    // Every behavior due to start is bound before any onStart runs, and started before any other
    // hook of the tick: an onStart calling one later in the order finds it bound, its own onStart
    // perhaps still to come, and every other hook finds them all started.
    if (startIfNeeded) {
        for (const EntityId id : m_tickList) {
            forEachBehaviorOf(scene, id, [&](Behavior& behavior) {
                if (!behavior.m_disabled && !behavior.m_started) behavior.bindContext(id, m_context);
            });
        }
        for (const EntityId id : m_tickList) {
            forEachBehaviorOf(scene, id, [&](Behavior& behavior) {
                if (!behavior.m_disabled) ensureStarted(behavior, id);
            });
        }
    }
    for (const EntityId id : m_tickList) {
        forEachBehaviorOf(scene, id, [&](Behavior& behavior) {
            if (behavior.m_disabled || !behavior.m_started) return;  // onStart threw, or is not due
            guard(behavior, hookName, [&] { (behavior.*hook)(dt); });
        });
    }
}

template<typename Arg>
void BehaviorSystem::dispatchEntityHook(
    Scene& scene,
    EntityId target,
    const std::remove_reference_t<Arg>& arg,
    const char* hookName,
    void (Behavior::*hook)(Arg)
) {
    forEachBehaviorOf(scene, target, [&](Behavior& behavior) {
        if (!behavior.m_started || behavior.m_disabled) return;
        guard(behavior, hookName, [&] { (behavior.*hook)(arg); });
    });
}

void BehaviorSystem::fireDestroy(Behavior& behavior) {
    // onDestroy mirrors onStart: never started, never destroyed.
    if (behavior.m_started) {
        behavior.runGuarded("onDestroy", [&] { behavior.onDestroy(); });
    }
    behavior.clearSubscriptions();
}

void BehaviorSystem::drainPendingDestroy(Scene& scene) {
    if (m_pendingDestroy.empty()) return;
    // Swapped out so a destroy requested from onDestroy drains next pass, not mid-walk.
    m_destroying.clear();
    m_destroying.swap(m_pendingDestroy);
    for (EntityId entity : m_destroying) {
        if (scene.isAlive(entity)) HierarchyOperations::destroyHierarchy(scene, entity);
    }
}

void BehaviorSystem::drainPendingSceneLoad(FrameContext& ctx) {
    // The server moved the session to another scene: this end follows it there, whatever a
    // behavior asks while the load waits.
    if (std::string next = ctx.net.takeSceneToLoad(); !next.empty()) m_followedScene = std::move(next);
    if (!m_followedScene.empty()) m_pendingSceneLoad = m_followedScene;

    // A restart is a load of the scene being played; a load asked in the same pass wins.
    const bool restart = m_pendingRestart;
    m_pendingRestart   = false;
    if (restart && m_pendingSceneLoad.empty()) {
        if (!m_loadedScene.empty()) {
            m_pendingSceneLoad = m_loadedScene;
        } else if (m_restartHandler) {
            m_restartHandler();
            return;
        } else if (!m_playedScene.empty()) {
            m_pendingSceneLoad = m_playedScene;
        } else {
            LOG_ERROR("A restart was asked for, but this scene came from no file the host kept");
            return;
        }
    }
    if (m_pendingSceneLoad.empty()) return;

    // The load holds this frame: put off until the frame shown is the loading screen's. A host
    // with no loading screen loads at once.
    if (ctx.loading && !ctx.loading->covering) {
        ctx.events.enqueue(LoadExpected{});
        return;
    }

    // Taken before endSession clears the queue; a load an outgoing onDestroy asks for
    // belongs to the ending session.
    std::string path;
    path.swap(m_pendingSceneLoad);
    const bool followed = !m_followedScene.empty();
    m_followedScene.clear();

    // onDestroy while they and their module are still alive; the load destroys them. A load
    // that fails leaves this world standing, and with it the file a restart reloads.
    const std::string playing = m_loadedScene;
    endSession(ctx.scene);

    const std::filesystem::path scenePath = ProjectPaths::projectRoot() / path;
    if (SceneSerializer::load(ctx.scene, ctx.resources, scenePath.string())) {
        LOG_INFO("Loaded scene '%s' on request", path.c_str());
        m_loadedScene = path;
        // A session's next world: a server tells its players which, a client says it holds it.
        // Named as the project names it, however the host or the game spelled it.
        const std::string named = ProjectPaths::toProjectRelative(scenePath.string());
        ctx.net.setWorld(fingerprintScene(scenePath), ctx.scene, named);
        // Nothing starts on a frozen frame, and the behavior that could resume the clock
        // went with the old scene.
        if (ctx.clock.isPaused() || ctx.clock.getTimeScale() <= 0.0f) {
            LOG_WARNING(
                "Scene '%s' was loaded while simulation time is frozen - nothing in it "
                "starts until the clock runs again",
                path.c_str()
            );
        }
    } else {
        // Transactional: the current scene stands; its behaviors restart next tick. A client the
        // server moved on cannot stay in it.
        m_loadedScene = playing;
        if (followed) ctx.net.worldFailed();
        LOG_ERROR(
            "Requested scene '%s' failed to load; staying in the current one",
            scenePath.string().c_str()
        );
    }
}

void BehaviorSystem::init(FrameContext& ctx) {
    // Every field but `ocean` and `water`, refreshed each tick and frame, must be session-stable, as
    // the FrameContext service block is.
    m_context.scene         = &ctx.scene;
    m_context.resources     = &ctx.resources;
    // Null on a host with no display, so a behavior knows it cannot read a device
    // instead of silently reading zeros.
    m_context.window        = ctx.window.isOpen() ? &ctx.window : nullptr;
    m_context.events        = &ctx.events;
    m_context.input         = &ctx.input;
    m_context.net           = &ctx.net;
    m_context.clock         = &ctx.clock;
    m_context.render        = &ctx.render;
    m_context.projectRender = &ctx.projectRender;
    m_context.chrome        = &ctx.chrome;
    m_context.debug         = ctx.debug;

    ctx.scene.addObserver(this);

    // Collect here; dispatch in update() once behaviors are started.
    m_collisionListener = m_context.events->subscribe<CollisionEvent>(
        [this](const CollisionEvent& e) { m_collisions.push_back(e); }
    );
    m_triggerListener = m_context.events->subscribe<TriggerEvent>(
        [this](const TriggerEvent& e) { m_triggers.push_back(e); }
    );
}

void BehaviorSystem::onEntityDestroyed(EntityId entity) {
    if (!m_context.scene) return;
    forEachBehaviorOf(*m_context.scene, entity, [](Behavior& behavior) { fireDestroy(behavior); });
}

void BehaviorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem");
    m_context.ocean = ctx.ocean;
    m_context.water = ctx.water;

    Scene& scene = ctx.scene;

    if (ctx.clock.getSimDelta() > 0.0f) {
        tickBehaviors(ctx, ctx.clock.getSimDelta(), "onUpdate", &Behavior::onUpdate, START_IF_NEEDED);

        m_dispatchCollisions.clear();
        m_dispatchCollisions.swap(m_collisions);
        for (const CollisionEvent& e : m_dispatchCollisions) {
            // A destroyed entity's Ended reaches only the survivor.
            const auto hook = collisionHook(e.phase);
            dispatchEntityHook(scene, e.a, Collision::seenBy(e, e.a), hook.name, hook.call);
            dispatchEntityHook(scene, e.b, Collision::seenBy(e, e.b), hook.name, hook.call);
        }
        m_dispatchTriggers.clear();
        m_dispatchTriggers.swap(m_triggers);
        for (const TriggerEvent& e : m_dispatchTriggers) {
            const auto hook = triggerHook(e.phase);
            dispatchEntityHook(scene, e.trigger, e.other, hook.name, hook.call);
        }
    } else {
        // Physics did not run either, so anything queued is stale; drop it.
        m_collisions.clear();
        m_triggers.clear();
    }

    // After onUpdate by rule; nothing starts here. The guard keeps the non-zero dt
    // promise only the first frame breaks. See docs/reference/scripting.md.
    const float realDelta = ctx.clock.getDeltaTime();
    if (realDelta > 0.0f) {
        tickBehaviors(ctx, realDelta, "onRealtimeUpdate", &Behavior::onRealtimeUpdate, NEVER_START);
    }

    drainPendingDestroy(scene);
    drainPendingSceneLoad(ctx);
}

void BehaviorSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem::fixed");
    m_context.ocean = ctx.ocean;
    m_context.water = ctx.water;

    // No pause gate: the accumulator is fed from the sim delta.
    tickBehaviors(ctx, ctx.clock.getFixedStep(), "onFixedUpdate", &Behavior::onFixedUpdate, START_IF_NEEDED);

    drainPendingDestroy(ctx.scene);
}

void BehaviorSystem::shutdown() {
    if (m_context.events) {
        m_context.events->unsubscribe<CollisionEvent>(m_collisionListener);
        m_context.events->unsubscribe<TriggerEvent>(m_triggerListener);
        m_collisionListener = 0;
        m_triggerListener   = 0;
    }

    if (!m_context.scene) return;
    endSession(*m_context.scene);            // onDestroy + drop subscriptions while the bus lives
    m_context.scene->removeObserver(this);   // avoid a callback into this dying system
}

void BehaviorSystem::endSession(Scene& scene) {
    // Collected first, as in tickBehaviors: an onDestroy may mutate the storage.
    std::vector<EntityId> scripted;
    if (auto* storage = scene.storage<ScriptComponent>()) {
        scripted.reserve(storage->size());
        storage->forEach([&](uint32_t entityIdx, ScriptComponent&) {
            scripted.push_back(scene.entityAt(entityIdx));
        });
    }

    for (const EntityId id : scripted) {
        forEachBehaviorOf(scene, id, [&](Behavior& behavior) {
            fireDestroy(behavior);
            behavior.m_started  = false;
            behavior.m_disabled = false;
        });
    }
    // Last, so a request an onDestroy made goes with the rest.
    m_pendingDestroy.clear();
    m_pendingSceneLoad.clear();
    m_followedScene.clear();
    m_pendingRestart = false;
    m_loadedScene.clear();
}

} // namespace Vkm::Engine
