#pragma once

#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/physics/physics_events.h"
#include "system/script/behavior.h"

namespace Engine {

class Scene;
class ResourceManager;
class EventSystem;

/**
 * @brief Drives the lifecycle of every entity's ScriptComponent behaviors.
 *
 * Registered at SystemStage::Simulation, before PhysicsSystem, so scripts set
 * up state for physics to integrate the same frame. Ticks only while the
 * Clock is running (ctx.clock.getSimDelta() > 0), so pause / step / Stop
 * apply uniformly: on an instance's first tick it injects the engine context
 * and calls onStart(), then onUpdate(simDeltaTime) every frame and
 * onFixedUpdate(fixedDeltaTime) every fixed tick.
 *
 * Subscribes to physics CollisionEvent / TriggerEvent and dispatches them to
 * the involved entities' onCollision / onTrigger hooks during update (after
 * onStart, before the deferred-destroy drain - so a collision handler may
 * destroy its own entity safely).
 *
 * Every hook runs under a catch net: a throwing behavior is reported via
 * reportError() and disabled, never fatal. onDestroy fires via endSession()
 * (play stop / shutdown) and destroyEntityBehaviors() (entity deletion, wired
 * through Scene::setOnEntityDestroy in init()).
 */
class BehaviorSystem : public System {
    public:
        explicit BehaviorSystem(EventSystem& events) : m_events(events) {}
        ~BehaviorSystem() override = default;

        BehaviorSystem(const BehaviorSystem& other) = delete;
        BehaviorSystem& operator=(const BehaviorSystem& other) = delete;

        BehaviorSystem(BehaviorSystem && other) = delete;
        BehaviorSystem& operator=(BehaviorSystem && other) = delete;

    public:
        void init(FrameContext& ctx) override;
        void update(FrameContext& ctx) override;
        void fixedUpdate(FrameContext& ctx) override;
        bool hasFixedUpdate() const override { return true; }
        void shutdown() override;

        /**
         * @brief Fire onDestroy on every started behavior in @p scene, drop their
         *        subscriptions, and reset their started/disabled flags.
         *
         * Tears down a whole scene's running behaviors: on play stop (before the
         * snapshot swaps the played scene away) and at shutdown (while the
         * EventSystem is still alive). Static because the editor's stop path has
         * no BehaviorSystem handle - friendship with Behavior is class-wide.
         */
        static void endSession(Scene& scene);

        /**
         * @brief Fire onDestroy on @p entity's started behaviors, just before
         *        its ScriptComponent is destroyed (entity deletion).
         *
         * Wired to Scene::setOnEntityDestroy() in init(), so it covers every
         * destroy path (raw Scene::destroyEntity and destroyHierarchy alike). A
         * no-op if the entity has no started behaviors.
         */
        static void destroyEntityBehaviors(Scene& scene, EntityId entity);

    private:
        /**
         * @brief Fire onStart once on @p behavior, binding the full context first.
         *
         * No-op if the behavior has already started. Runs under the catch net:
         * a throw is reported and the behavior is disabled.
         *
         * @param behavior The behavior to start.
         * @param entity   The entity owning @p behavior, bound into its context.
         * @param ctx      Frame context supplying scene/resources/input/events.
         */
        void ensureStarted(Behavior& behavior, EntityId entity, FrameContext& ctx);
        /**
         * @brief Shared per-tick walk for update/fixedUpdate: skip null/disabled,
         *        ensureStarted, re-check disabled, then guard the @p hook (a
         *        void(float) per-frame hook) with @p dt. Caller drains deferred
         *        destroys at its own (callsite-specific) point afterwards.
         */
        void tickBehaviors(FrameContext& ctx, float dt, const char* hookName, void (Behavior::*hook)(float));
        /**
         * @brief Deliver an entity-targeted hook (onCollision/onTrigger) to a
         *        target entity's started behaviors.
         *
         * @param scene    Scene holding the behaviors to dispatch to.
         * @param target   Entity whose started behaviors receive the hook.
         * @param other    The other entity passed to the hook (the contact partner).
         * @param hookName Human-readable hook name, used in error reporting.
         * @param hook     The void(EntityId) member hook to invoke on each behavior.
         */
        void dispatchEntityHook(Scene& scene, EntityId target, EntityId other,
                                const char* hookName, void (Behavior::*hook)(EntityId));
        /**
         * @brief Apply queued destroy() requests via destroyHierarchy.
         *
         * Run after the hook pass so a behavior can safely destroy its own
         * entity without freeing the ScriptComponent mid-iteration.
         *
         * @param scene Scene the pending entities are destroyed from.
         */
        void drainPendingDestroy(Scene& scene);
        /**
         * @brief Run a hook body under the catch net.
         *
         * On throw it logs the failure via reportError() and disables the
         * behavior so it is skipped thereafter.
         *
         * @tparam Fn       Callable type invoked as the hook body.
         * @param  behavior The behavior whose hook is running (disabled on throw).
         * @param  hookName Human-readable hook name, used in error reporting.
         * @param  fn       The hook body to invoke.
         */
        template<typename Fn>
        static void guard(Behavior& behavior, const char* hookName, Fn&& fn);
        /**
         * @brief Fire onDestroy on a started behavior.
         *
         * Catches a throwing onDestroy but does not disable the behavior, since
         * it is being torn down anyway.
         *
         * @param behavior The behavior to send onDestroy to.
         */
        static void fireDestroy(Behavior& behavior);

        EventSystem& m_events;  ///< Injected into behaviors; also the bus we listen on.

        /**
         * @brief Physics events collected via subscriptions, dispatched to hooks
         *        in update().
         */
        std::vector<CollisionEvent> m_collisions;
        std::vector<TriggerEvent>   m_triggers;

        /**
         * @brief Entities behaviors asked to destroy() this pass; drained after the
         * hook loop so a self-destroy can't free its ScriptComponent mid-iterate.
         */
        std::vector<EntityId> m_pendingDestroy;

        /**
         * @brief Cached in init() so shutdown() (which gets no FrameContext) can still
         * walk the scene. The Scene object is stable for the engine's lifetime.
         */
        Scene* m_scene = nullptr;
};

} // namespace Engine
