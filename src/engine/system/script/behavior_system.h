#pragma once

#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/physics/physics_events.h"  // CollisionEvent / TriggerEvent
#include "system/script/behavior.h"          // Behavior (pointer-to-member hook params)

namespace Engine {

class Scene;
class ResourceManager;
class EventSystem;

/**
 * @brief Drives the lifecycle of every entity's ScriptComponent behaviors.
 *
 * Registered at SystemStage::Simulation, before PhysicsSystem, so scripts set
 * up state for physics to integrate the same frame. Ticks only while the
 * SimulationClock is running (ctx.simDeltaTime > 0), so pause / step / Stop
 * apply uniformly: on an instance's first tick it injects the engine context
 * and calls onStart(), then onUpdate(simDeltaTime) every frame and
 * onFixedUpdate(fixedDeltaTime) every fixed tick.
 *
 * Subscribes to physics CollisionEvent / TriggerEvent and dispatches them to
 * the involved entities' onCollision / onTrigger hooks during update (after
 * onStart, before the deferred-destroy drain - so a collision handler may
 * destroy its own entity safely).
 *
 * Every hook runs under a catch net: a throwing behavior is logged to
 * BehaviorErrorLog and disabled, never fatal. onDestroy fires via endSession()
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
        /// Fire onStart once (binding the full context first), catching + disabling on throw.
        void ensureStarted(Behavior& behavior, EntityId entity, FrameContext& ctx);
        /// Deliver onCollision/onTrigger (a void(EntityId) hook) to a target entity's started behaviors.
        void dispatchEntityHook(Scene& scene, EntityId target, EntityId other,
                                const char* hookName, void (Behavior::*hook)(EntityId));
        /// Apply queued destroy() requests via destroyHierarchy, after the hook pass.
        void drainPendingDestroy(Scene& scene);
        /// Run a hook body under the catch net: log + disable the behavior on throw.
        template<typename Fn>
        static void guard(Behavior& behavior, const char* hookName, Fn&& fn);
        /// Fire onDestroy on a started behavior, catching (but not disabling - it's going away).
        static void fireDestroy(Behavior& behavior);

        EventSystem& m_events;  ///< Injected into behaviors; also the bus we listen on.

        /// Physics events collected via subscriptions, dispatched to hooks in update().
        std::vector<CollisionEvent> m_collisions;
        std::vector<TriggerEvent>   m_triggers;

        /// Entities behaviors asked to destroy() this pass; drained after the
        /// hook loop so a self-destroy can't free its ScriptComponent mid-iterate.
        std::vector<EntityId> m_pendingDestroy;

        /// Cached in init() so shutdown() (which gets no FrameContext) can still
        /// walk the scene. The Scene object is stable for the engine's lifetime.
        Scene* m_scene = nullptr;
};

} // namespace Engine
