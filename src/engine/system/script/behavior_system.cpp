#define VKM_LOG_CATEGORY "SCRIPT"

#include "system/script/behavior_system.h"

#include <algorithm>
#include <filesystem>
#include <type_traits>
#include <utility>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "base/profile/profiler.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "net/net_session.h"
#include "vkm_platform/window/window_manager.h"
#include "base/event/event_bus.h"
#include "io/project/roots.h"
#include "io/scene/scene_fingerprint.h"
#include "io/scene/scene_serializer.h"
#include "core/frame/loading_events.h"
#include "core/frame/loading_frame.h"
#include "core/frame/splash_frame.h"
#include "system/script/behavior_base.h"
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

        if (BehaviorBase* behavior = sc->behaviors[i].get()) fn(*behavior);
    }
}

/// The hook a contact phase is delivered to, and its name for an error report.
template<typename Arg>
struct ContactHook {
    const char* name;
    void (BehaviorBase::*call)(Arg);
};

ContactHook<const Collision&> collisionHook(ContactPhase phase) {
    switch (phase) {
        case ContactPhase::Began:  return {"onCollisionEnter", &BehaviorBase::onCollisionEnter};
        case ContactPhase::Stayed: return {"onCollisionStay",  &BehaviorBase::onCollisionStay};
        case ContactPhase::Ended:  break;
    }
    return {"onCollisionExit", &BehaviorBase::onCollisionExit};
}

ContactHook<EntityId> triggerHook(ContactPhase phase) {
    switch (phase) {
        case ContactPhase::Began:  return {"onTriggerEnter", &BehaviorBase::onTriggerEnter};
        case ContactPhase::Stayed: return {"onTriggerStay",  &BehaviorBase::onTriggerStay};
        case ContactPhase::Ended:  break;
    }
    return {"onTriggerExit", &BehaviorBase::onTriggerExit};
}

} // namespace

template<typename Fn>
void BehaviorSystem::guard(BehaviorBase& behavior, const char* hookName, Fn&& fn) {
    if (behavior.runGuarded(hookName, std::forward<Fn>(fn))) {
        behavior.m_disabled = true;
    }
}

void BehaviorSystem::ensureStarted(BehaviorBase& behavior, EntityId entity) {
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
    void (BehaviorBase::*hook)(float),
    bool startIfNeeded
) {
    Scene& scene  = ctx.scene;
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
            forEachBehaviorOf(scene, id, [&](BehaviorBase& behavior) {
                if (!behavior.m_disabled && !behavior.m_started) behavior.bindContext(id, m_context);
            });
        }
        for (const EntityId id : m_tickList) {
            forEachBehaviorOf(scene, id, [&](BehaviorBase& behavior) {
                if (!behavior.m_disabled) ensureStarted(behavior, id);
            });
        }
    }
    for (const EntityId id : m_tickList) {
        forEachBehaviorOf(scene, id, [&](BehaviorBase& behavior) {
            if (behavior.m_disabled || !behavior.m_started) return;  // onStart threw, or is not due
            guard(behavior, hookName, [&] { (behavior.*hook)(dt); });
            // after() counts live ticks, as destroyAfter does.
            if (m_context.ticking && !replaying && !behavior.m_after.empty()) runDue(behavior, dt);
        });
    }
}

template<typename Arg>
void BehaviorSystem::dispatchEntityHook(
    Scene& scene,
    EntityId target,
    const std::remove_reference_t<Arg>& arg,
    const char* hookName,
    void (BehaviorBase::*hook)(Arg)
) {
    forEachBehaviorOf(scene, target, [&](BehaviorBase& behavior) {
        if (!behavior.m_started || behavior.m_disabled) return;
        guard(behavior, hookName, [&] { (behavior.*hook)(arg); });
    });
}

void BehaviorSystem::fireDestroy(BehaviorBase& behavior) {
    // onDestroy mirrors onStart: never started, never destroyed.
    if (behavior.m_started) {
        behavior.runGuarded("onDestroy", [&] { behavior.onDestroy(); });
    }
    behavior.clearSubscriptions();
}

void BehaviorSystem::runDue(BehaviorBase& behavior, float step) {
    // Swapped out, as a call may ask for another.
    std::vector<BehaviorBase::Delayed> waiting;
    waiting.swap(behavior.m_after);
    for (BehaviorBase::Delayed& delayed : waiting) {
        delayed.seconds -= step;
        if (delayed.seconds > 0.0f) behavior.m_after.push_back(std::move(delayed));
        else guard(behavior, "after", delayed.fn);
    }
}

void BehaviorSystem::countDownTimedDestroy(float step) {
    if (m_context.timedDestroy.empty()) return;
    const auto due = [&](TimedDestroy& timed) {
        timed.seconds -= step;
        if (timed.seconds > 0.0f) return false;
        m_context.pendingDestroy.push_back(timed.entity);
        return true;
    };
    const auto kept = std::remove_if(m_context.timedDestroy.begin(), m_context.timedDestroy.end(), due);
    m_context.timedDestroy.erase(kept, m_context.timedDestroy.end());
}

void BehaviorSystem::drainPendingDestroy(Scene& scene) {
    if (m_context.pendingDestroy.empty()) return;
    // Swapped out so a destroy requested from onDestroy drains next pass, not mid-walk.
    m_destroying.clear();
    m_destroying.swap(m_context.pendingDestroy);
    for (EntityId entity : m_destroying) {
        if (scene.isAlive(entity)) HierarchyOperations::destroyHierarchy(scene, entity);
    }
}

void BehaviorSystem::drainPendingSceneLoad(FrameContext& ctx) {
    // The server moved the session to another scene: this end follows it there, whatever a
    // behavior asks while the load waits.
    if (std::string next = ctx.net.takeSceneToLoad(); !next.empty()) m_followedScene = std::move(next);
    if (!m_followedScene.empty()) m_context.pendingSceneLoad = m_followedScene;

    // A restart is a load of the scene being played; a load asked in the same pass wins.
    const bool restart = m_context.pendingRestart;
    m_context.pendingRestart = false;
    if (restart && m_context.pendingSceneLoad.empty()) {
        if (!m_loadedScene.empty()) {
            m_context.pendingSceneLoad = m_loadedScene;
        } else if (m_restartHandler) {
            m_restartHandler();
            return;
        } else if (!m_playedScene.empty()) {
            m_context.pendingSceneLoad = m_playedScene;
        } else {
            LOG_ERROR("A restart was asked for, but this scene came from no file the host kept");
            return;
        }
    }
    if (m_context.pendingSceneLoad.empty()) return;

    // The load holds this frame: put off until the frame shown is the loading screen's. A host
    // with no loading screen loads at once.
    if (ctx.loading && !ctx.loading->covering) {
        ctx.events.enqueue(LoadExpected{});
        return;
    }

    // Taken before endSession clears the queue; a load an outgoing onDestroy asks for
    // belongs to the ending session.
    std::string path;
    path.swap(m_context.pendingSceneLoad);
    const bool followed = !m_followedScene.empty();
    m_followedScene.clear();

    // onDestroy while they and their module are still alive; the load destroys them. A load
    // that fails leaves this world standing, and with it the file a restart reloads.
    const std::string playing = m_loadedScene;
    endSession(ctx.scene);

    const std::filesystem::path scenePath = Roots::projectRoot() / path;
    if (SceneSerializer::load(ctx.scene, ctx.resources, scenePath.string())) {
        LOG_INFO("Loaded scene '%s' on request", path.c_str());
        m_loadedScene = path;
        // A session's next world: a server tells its players which, a client says it holds it.
        // Named as the project names it, however the host or the game spelled it.
        const std::string named = Roots::toProjectRelative(scenePath.string());
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
    // The session's, so a behavior may keep reaching it from a listener between frames.
    m_context.frame = &ctx;

    ctx.scene.addObserver(this);

    // Collect here; dispatch in update() once behaviors are started.
    m_collisionListener = ctx.events.subscribe<CollisionEvent>(
        [this](const CollisionEvent& e) { m_collisions.push_back(e); }
    );
    m_triggerListener = ctx.events.subscribe<TriggerEvent>(
        [this](const TriggerEvent& e) { m_triggers.push_back(e); }
    );
}

void BehaviorSystem::onEntityDestroyed(EntityId entity) {
    if (!m_context.frame) return;
    forEachBehaviorOf(m_context.frame->scene, entity, [](BehaviorBase& behavior) { fireDestroy(behavior); });
}

void BehaviorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem");

    Scene& scene = ctx.scene;
    m_context.covered = (ctx.splash && ctx.splash->isShowing()) || (ctx.loading && ctx.loading->isShowing());

    if (ctx.clock.getSimDelta() > 0.0f) {
        tickBehaviors(ctx, ctx.clock.getSimDelta(), "onUpdate", &BehaviorBase::onUpdate, START_IF_NEEDED);

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
        tickBehaviors(ctx, realDelta, "onRealtimeUpdate", &BehaviorBase::onRealtimeUpdate, NEVER_START);
    }

    drainPendingDestroy(scene);
    drainPendingSceneLoad(ctx);
}

void BehaviorSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("BehaviorSystem::fixed");

    // No pause gate: the accumulator is fed from the sim delta.
    const float step = ctx.clock.getFixedStep();
    m_context.ticking = true;
    tickBehaviors(ctx, step, "onFixedUpdate", &BehaviorBase::onFixedUpdate, START_IF_NEEDED);
    m_context.ticking = false;

    // A replay re-runs a moment whose destroys already happened.
    if (!ctx.net.replaying()) countDownTimedDestroy(step);

    drainPendingDestroy(ctx.scene);
}

void BehaviorSystem::shutdown() {
    if (!m_context.frame) return;
    m_context.frame->events.unsubscribe<CollisionEvent>(m_collisionListener);
    m_context.frame->events.unsubscribe<TriggerEvent>(m_triggerListener);
    m_collisionListener = 0;
    m_triggerListener   = 0;

    endSession(m_context.frame->scene);            // onDestroy + drop subscriptions while the bus lives
    m_context.frame->scene.removeObserver(this);   // avoid a callback into this dying system
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
        forEachBehaviorOf(scene, id, [&](BehaviorBase& behavior) {
            fireDestroy(behavior);
            behavior.m_started  = false;
            behavior.m_disabled = false;
        });
    }
    // Last, so a request an onDestroy made goes with the rest.
    m_context.pendingDestroy.clear();
    m_context.timedDestroy.clear();
    m_context.pendingSceneLoad.clear();
    m_followedScene.clear();
    m_context.pendingRestart = false;
    m_loadedScene.clear();
}

} // namespace Vkm::Engine
