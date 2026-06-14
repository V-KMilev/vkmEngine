#pragma once

#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "system/script/behavior.h"  // Behavior (pointer-to-member hook params)

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
 * Behaviors reach the engine through the injected context (scene, resources,
 * input, events) and spawn()/destroy() - destroy is deferred and drained here
 * after the hook pass, so a behavior may destroy its own entity safely.
 *
 * Every hook runs under a catch net: a throwing behavior is logged to
 * BehaviorErrorLog and disabled, never fatal. onDestroy fires via endSession()
 * (play stop / shutdown) and destroyEntityBehaviors() (entity deletion).
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
         * @brief Fire onDestroy on every started behavior in @p scene and reset
         *        their started/disabled flags.
         *
         * Tears down a whole scene's running behaviors: on play stop (before the
         * snapshot swaps the played scene away) and at shutdown. Static because
         * the editor's stop path has no BehaviorSystem handle - friendship with
         * Behavior is class-wide, so a static reaches its flags.
         */
        static void endSession(Scene& scene);

        /**
         * @brief Fire onDestroy on @p entity's started behaviors, just before
         *        its ScriptComponent is destroyed (entity deletion).
         *
         * Called from HierarchyOperations::destroyHierarchy - the chokepoint for
         * entity + subtree destruction. A no-op if the entity has no started
         * behaviors (e.g. deleted in edit mode, where nothing ever started).
         */
        static void destroyEntityBehaviors(Scene& scene, EntityId entity);

    private:
        /// Fire onStart once (binding the full context first), catching + disabling on throw.
        void ensureStarted(Behavior& behavior, EntityId entity, FrameContext& ctx);
        /// Apply queued destroy() requests via destroyHierarchy, after the hook pass.
        void drainPendingDestroy(Scene& scene);
        /// Invoke a per-frame hook (onUpdate / onFixedUpdate), catching + disabling on throw.
        static void invoke(Behavior& behavior, const char* hookName, void (Behavior::*hook)(float), float dt);
        /// Fire onDestroy on a started behavior, catching (but not disabling - it's going away).
        static void fireDestroy(Behavior& behavior);

        EventSystem& m_events;  ///< Injected into behaviors for gameplay pub/sub.

        /// Entities behaviors asked to destroy() this pass; drained after the
        /// hook loop so a self-destroy can't free its ScriptComponent mid-iterate.
        std::vector<EntityId> m_pendingDestroy;

        /// Cached in init() so shutdown() (which gets no FrameContext) can still
        /// walk the scene. The Scene object is stable for the engine's lifetime -
        /// scene swaps replace its contents, not the object.
        Scene* m_scene = nullptr;
};

} // namespace Engine
