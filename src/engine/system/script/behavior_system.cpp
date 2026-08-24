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
                                   void (Behavior::*hook)(float), bool startIfNeeded) {
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
            if (startIfNeeded) {
                ensureStarted(*behavior, id);
                if (behavior->m_disabled) continue;  // onStart threw
            } else if (!behavior->m_started) {
                continue;
            }
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
    // onDestroy mirrors onStart: never started, never destroyed.
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

    // Take the request before the endSession below, which clears the queue:
    // this load is the one being served, and a further one asked for by an
    // outgoing behavior's onDestroy belongs to the session that is ending
    // rather than to the scene arriving.
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
        // The drain runs on a frozen frame, but nothing the load brought in
        // will start on one - a behavior starts on a simulation tick. The
        // behavior that asked has just gone with the old scene, so unless
        // something outside gameplay resumes the clock the new world renders
        // and never runs. Said here because there is no other symptom.
        if (ctx.clock.isPaused() || ctx.clock.getTimeScale() <= 0.0f) {
            LOG_WARNING("Scene '%s' was loaded while simulation time is frozen - nothing in it "
                        "starts until the clock runs again", path.c_str());
        }
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
    m_context.clock     = &ctx.clock;

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

    Scene& scene = ctx.scene;

    if (ctx.clock.getSimDelta() > 0.0f) {
        tickBehaviors(ctx, ctx.clock.getSimDelta(), "onUpdate", &Behavior::onUpdate,
                      /*startIfNeeded*/ true);

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
    } else {
        // Physics did not run either, so anything still queued describes a world
        // state older than the pause. Drop it rather than deliver it stale.
        m_collisions.clear();
        m_triggers.clear();
    }

    // Real time, pause included, and after onUpdate so a realtime hook reading
    // world state sees what the simulation produced this frame. Nothing starts
    // here - a behavior belongs to a play session and only simulation time
    // begins one, which is also what keeps the editor's Edit mode inert.
    //
    // The guard is what makes the promise in the hook's docs true rather than
    // nearly true: gameplay is never handed a zero dt. Only the engine's very
    // first frame measures one, having nothing to measure against yet.
    const float realDelta = ctx.clock.getDeltaTime();
    if (realDelta > 0.0f) {
        tickBehaviors(ctx, realDelta, "onRealtimeUpdate", &Behavior::onRealtimeUpdate,
                      /*startIfNeeded*/ false);
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
    tickBehaviors(ctx, ctx.clock.getFixedStep(), "onFixedUpdate", &Behavior::onFixedUpdate,
                  /*startIfNeeded*/ true);

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

    // The queues these behaviors defer into, taken off one of them: a bound
    // context is how a behavior reaches them, and the editor's stop path calls
    // this with no BehaviorSystem in hand. Null only when nothing ever started,
    // which is also when nothing can have queued.
    BehaviorContext* session = nullptr;

    storage->forEach([&](uint32_t, ScriptComponent& sc) {
        for (auto& behavior : sc.behaviors) {
            if (!behavior) continue;
            if (behavior->m_ctx) session = behavior->m_ctx;
            fireDestroy(*behavior);
            behavior->m_started  = false;
            behavior->m_disabled = false;
        }
    });
    if (!session) return;

    // What an onDestroy just asked for named the world that is going away, and
    // dies with it. Left queued it drains on the next frame - which since the
    // realtime pass exists happens while paused, and in the editor paused is
    // Edit mode: Stop would load a gameplay scene over the authored one, or
    // destroy whichever entity inherited a slot the played scene had freed. An
    // entity id does not go stale across the swap, it re-aims.
    session->pendingDestroy->clear();
    session->pendingSceneLoad->clear();
}

void BehaviorSystem::destroyEntityBehaviors(Scene& scene, EntityId entity) {
    if (!scene.has<ScriptComponent>(entity)) return;
    ScriptComponent& sc = scene.get<ScriptComponent>(entity);
    for (auto& behavior : sc.behaviors) {
        if (behavior) fireDestroy(*behavior);
    }
}

} // namespace Vkm::Engine
