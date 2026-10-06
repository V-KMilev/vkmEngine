#pragma once

#include <type_traits>
#include <vector>

#include "core/system.h"
#include "ecs/entity.h"
#include "ecs/scene_observer.h"
#include "system/physics/physics_events.h"
#include "system/script/behavior.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
class EventBus;

/**
 * @brief Drives the lifecycle of every entity's ScriptComponent behaviors.
 *
 * Runs before PhysicsSystem in Simulation, so physics integrates script state the same
 * frame. Destroy requests drain after each fixed tick and the frame pass; loadScene()
 * after the frame pass only. Contacts are dispatched after onStart and before the
 * destroy drain, so a handler may destroy its own entity. A throwing hook is reported
 * through reportError() and the behavior disabled.
 */
class BehaviorSystem : public System, public ISceneObserver {
    public:
        BehaviorSystem() {
            m_context.pendingDestroy   = &m_pendingDestroy;
            m_context.pendingSceneLoad = &m_pendingSceneLoad;
        }
        ~BehaviorSystem() override = default;

        BehaviorSystem(const BehaviorSystem& other) = delete;
        BehaviorSystem& operator=(const BehaviorSystem& other) = delete;

        BehaviorSystem(BehaviorSystem && other) = delete;
        BehaviorSystem& operator=(BehaviorSystem && other) = delete;

    public:
        void init(FrameContext& ctx) override;
        void update(FrameContext& ctx) override;
        void fixedUpdate(FrameContext& ctx) override;

        /**
         * @brief Re-run during a replay: it moves the world from state and command, repeatably.
         *
         * @return Always true.
         */
        bool isReplayed() const override { return true; }

        void shutdown() override;

        /**
         * @brief ISceneObserver: fire onDestroy on @p entity's behaviors before its ScriptComponent goes.
         *
         * Runs inside Scene::destroyEntity, whatever called it.
         *
         * @param entity The entity being destroyed.
         */
        void onEntityDestroyed(EntityId entity) override;

        /**
         * @brief End the session: onDestroy on every started behavior, reset, and discard queued requests.
         *
         * Drops subscriptions and the started/disabled flags. Call while the module is
         * loaded and the EventBus alive. Queued destroy() / loadScene() requests named the
         * world being torn down, so they go too.
         *
         * @param scene Scene whose session ends.
         */
        void endSession(Scene& scene);

        /**
         * @brief Fire onDestroy on @p entity's started behaviors, before its ScriptComponent goes.
         *
         * @param scene Scene holding the entity.
         * @param entity Entity whose behaviors are told.
         */
        static void destroyEntityBehaviors(Scene& scene, EntityId entity);

    private:
        /**
         * @brief Run @p behavior's onStart if it has not run yet.
         *
         * @param behavior Behavior to start.
         * @param entity   Entity it is attached to.
         */
        void ensureStarted(Behavior& behavior, EntityId entity);

        /**
         * @brief Drive @p hook on every enabled behavior in the scene; the caller drains destroys.
         *
         * @param ctx           Frame context supplying the scene to walk.
         * @param dt            Elapsed time handed to the hook.
         * @param hookName      Hook name for error reports.
         * @param hook          Member hook to invoke on each behavior.
         * @param startIfNeeded Start unstarted behaviors (simulation passes) or skip
         *                      them (the realtime pass, which must not begin a session).
         */
        void tickBehaviors(
            FrameContext& ctx,
            float dt,
            const char* hookName,
            void (Behavior::*hook)(float),
            bool startIfNeeded
        );

        /**
         * @brief Drive a contact hook on every enabled behavior of @p target.
         *
         * @tparam Arg     What the hook takes: a Collision, or the other entity.
         * @param scene    Scene holding the target.
         * @param target   Entity whose behaviors hear the contact.
         * @param arg      The contact as @p target sees it.
         * @param hookName Hook name for error reports.
         * @param hook     Member hook to invoke.
         */
        template<typename Arg>
        void dispatchEntityHook(
            Scene& scene,
            EntityId target,
            const std::remove_reference_t<Arg>& arg,
            const char* hookName,
            void (Behavior::*hook)(Arg)
        );

        /**
         * @brief Apply queued destroy() requests, after the hook pass so none frees a component mid-walk.
         *
         * @param scene Scene the pending entities are destroyed from.
         */
        void drainPendingDestroy(Scene& scene);

        /**
         * @brief Load the scene a behavior asked for, if one did.
         *
         * @param ctx Frame context whose scene and resources are replaced.
         */
        void drainPendingSceneLoad(FrameContext& ctx);

        /**
         * @brief Run a hook body; a throw is reported via reportError() and disables the behavior.
         *
         * @tparam Fn       Hook body type.
         * @param  behavior Behavior whose hook is running.
         * @param  hookName Hook name for error reports.
         * @param  fn       Hook body to invoke.
         */
        template<typename Fn>
        static void guard(Behavior& behavior, const char* hookName, Fn&& fn);

        /**
         * @brief Fire onDestroy on @p behavior if it started, then drop its subscriptions.
         *
         * A throw is caught without disabling: the session is over either way.
         *
         * @param behavior Behavior to send onDestroy to.
         */
        static void fireDestroy(Behavior& behavior);

    private:
        std::vector<CollisionEvent> m_collisions;
        std::vector<TriggerEvent>   m_triggers;

        /// Swapped out of the queues above, since a handler may queue more; members for capacity.
        std::vector<CollisionEvent> m_dispatchCollisions;
        std::vector<TriggerEvent>   m_dispatchTriggers;

        /// Kept so shutdown can drop them: both capture `this`, and the bus outlives this system.
        ListenerId m_collisionListener = 0;
        ListenerId m_triggerListener   = 0;

        /**
         * @brief Entities to tick this pass, snapshotted before any hook runs.
         *
         * A hook adding a ScriptComponent may reallocate the storage, so each entity is
         * re-resolved from the snapshot. A member for its capacity.
         */
        std::vector<EntityId> m_tickList;

        std::vector<EntityId> m_pendingDestroy;
        /// Walked by drainPendingDestroy; a member for its capacity.
        std::vector<EntityId> m_destroying;
        std::string           m_pendingSceneLoad;  ///< Scene a behavior asked for; empty when none.

        BehaviorContext m_context;
};

} // namespace Vkm::Engine
