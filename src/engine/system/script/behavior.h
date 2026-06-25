#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "system/event/event_system.h"

namespace Engine {

class Scene;
class ResourceManager;
class InputHandle;
class BehaviorSystem;
class BehaviorFieldVisitor;

/**
 * @brief Base class for native C++ gameplay behaviors.
 *
 * The engine's MonoBehaviour / ActorComponent analogue: subclass it, override
 * the lifecycle hooks, and attach instances to an entity through a
 * ScriptComponent. BehaviorSystem drives the hooks during play mode and injects
 * the engine context before onStart(), so hooks reach the engine via
 * m_scene / m_resources / m_input / m_events and the spawn()/destroy() helpers.
 *
 * Non-copyable and non-movable: instances are owned by unique_ptr inside
 * ScriptComponent. Deep-copy for entity duplication goes through clone().
 *
 * Events: emit/enqueue via m_events directly. To listen, use subscribe<E>() -
 * it auto-unsubscribes when the behavior is destroyed, so there's no manual
 * cleanup (a raw m_events->subscribe would dangle once this instance dies).
 */
class Behavior {
    public:
        Behavior() = default;
        virtual ~Behavior() { clearSubscriptions(); }

        Behavior(const Behavior& other) = delete;
        Behavior& operator=(const Behavior& other) = delete;

        Behavior(Behavior && other) = delete;
        Behavior& operator=(Behavior && other) = delete;

    public:
        virtual void onStart()             {}  ///< First tick this instance runs in play mode.
        virtual void onUpdate(float dt)    {}  ///< Variable step; dt is simulation seconds.
        virtual void onFixedUpdate(float dt) {}  ///< Fixed step; dt = fixedDeltaTime. Opt-in.
        virtual void onCollision(EntityId other) {}  ///< A non-trigger contact with `other` this tick.
        virtual void onTrigger(EntityId other)   {}  ///< This entity's trigger overlapped `other` this tick.
        virtual void onDestroy()           {}  ///< Instance torn down (entity removed / play stopped / shutdown).

        /**
         * @brief Stable type name, identical to this type's BehaviorRegistry key.
         *
         * Single source of truth shared with registration: a subclass declares
         * `static constexpr const char* TYPE_NAME` and returns it here, and
         * BehaviorRegistry::registerBehavior<T>() keys off the same constant.
         * Serialization round-trips the behavior by this name.
         */
        virtual const char* typeName() const = 0;

        /**
         * @brief Visit the behavior's reflected authoring fields.
         *
         * The editor inspector and the serializer use this to read/write fields
         * through a `Behavior*` without knowing the concrete type. Default does
         * nothing; ReflectedBehavior generates it from the VKM_REFLECT markup.
         */
        virtual void visitFields(BehaviorFieldVisitor& visitor) {}

        /**
         * @brief Deep copy for entity duplication.
         *
         * Copy only authored fields; the engine context and started flag are
         * rebound on the new instance by BehaviorSystem.
         */
        virtual std::unique_ptr<Behavior> clone() const = 0;

    protected:
        /**
         * @brief Create a new (empty) entity; add components to it via m_scene. Safe
         * to call from a hook, with one caveat: attaching a ScriptComponent to
         * the new entity mid-hook can reallocate the behavior storage being
         * iterated - do that kind of structural script wiring outside the hot
         * loop (e.g. at scene setup), not inline.
         */
        Entity spawn();

        /**
         * @brief Destroy @p entity (and its subtree) - deferred until after the current
         * hook pass, so destroying your own entity is safe. Fires onDestroy on
         * the affected behaviors. Routed through HierarchyOperations.
         */
        void destroy(EntityId entity);

        /**
         * @brief Subscribe to events of type EventT for this behavior's lifetime. The
         * subscription is dropped automatically when the behavior is destroyed
         * (or the play session ends), so there is nothing to clean up by hand.
         */
        template<typename EventT>
        void subscribe(std::function<void(const EventT&)> callback) {
            if (!m_events) return;
            EventSystem* events = m_events;
            const EventSystem::ListenerId id = events->subscribe<EventT>(std::move(callback));
            m_subscriptions.push_back([events, id]() { events->unsubscribe<EventT>(id); });
        }

        EntityId           m_entity{};
        Scene*             m_scene     = nullptr;
        ResourceManager*   m_resources = nullptr;
        const InputHandle* m_input     = nullptr;  ///< Keyboard/mouse query (read-only).
        EventSystem*       m_events    = nullptr;  ///< Gameplay pub/sub (emit/enqueue/subscribe).

    private:
        friend class BehaviorSystem;

        /** @brief Injected by BehaviorSystem before onStart so hooks can reach the engine. */
        void bindContext(
            EntityId entity,
            Scene& scene,
            ResourceManager& resources,
            const InputHandle& input,
            EventSystem& events,
            std::vector<EntityId>& pendingDestroy
        ) {
            m_entity         = entity;
            m_scene          = &scene;
            m_resources      = &resources;
            m_input          = &input;
            m_events         = &events;
            m_pendingDestroy = &pendingDestroy;
        }

        /**
         * @brief Drop all subscribe<E>() listeners. Run from the destructor and, while
         * the EventSystem is guaranteed alive, by BehaviorSystem::endSession at
         * play stop / shutdown (so it never unsubscribes from a dead bus).
         */
        void clearSubscriptions() {
            for (auto& unsubscribe : m_subscriptions) unsubscribe();
            m_subscriptions.clear();
        }

        std::vector<std::function<void()>> m_subscriptions;  ///< Auto-unsubscribe thunks.
        std::vector<EntityId>* m_pendingDestroy = nullptr;   ///< BehaviorSystem's deferred-destroy queue.
        bool m_started  = false;  ///< onStart already fired (managed by BehaviorSystem).
        bool m_disabled = false;  ///< Set after a hook threw; BehaviorSystem then skips it.
};

} // namespace Engine
