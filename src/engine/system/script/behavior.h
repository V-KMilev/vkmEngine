#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "platform/window/window_manager.h"
#include "core/event/event_bus.h"

namespace Engine {

class ResourceManager;
class BehaviorSystem;
class BehaviorFieldVisitor;

/**
 * @brief Everything gameplay code may reach, bundled behind one pointer.
 *
 * Owned by the BehaviorSystem and stable for the whole session - unlike the
 * per-frame FrameContext, whose lifetime ends every frame. That stability is
 * what lets a behavior's subscribe() lambdas keep using the context after the
 * hook pass that created them has returned.
 *
 * This is also the gameplay capability surface: a field belongs here exactly
 * when behaviors are meant to use it (which is why the Clock, for example, is
 * absent - a behavior that pauses the sim stops ticking and can never
 * unpause). Growing it costs one field here plus one accessor on Behavior;
 * bindContext() never changes again.
 */
struct BehaviorContext {
    Scene*                 scene          = nullptr;
    ResourceManager*       resources      = nullptr;
    WindowManager*         window         = nullptr;
    EventBus*              events         = nullptr;
    std::vector<EntityId>* pendingDestroy = nullptr;
};

/**
 * @brief Base class for native C++ gameplay behaviors.
 *
 * The engine's MonoBehaviour / ActorComponent analogue: subclass it, override
 * the lifecycle hooks, and attach instances to an entity through a
 * ScriptComponent. BehaviorSystem drives the hooks during play mode and binds
 * its BehaviorContext before onStart(), so hooks reach the engine through
 * context() and the spawn()/destroy()/subscribe() helpers.
 *
 * Non-copyable and non-movable: instances are owned by unique_ptr inside
 * ScriptComponent. Deep-copy for entity duplication goes through clone().
 *
 * Events: emit/enqueue via context().events directly. To listen, use
 * subscribe<E>() - it auto-unsubscribes when the behavior is destroyed, so
 * there's no manual cleanup (a raw subscribe on the bus would dangle once this
 * instance dies). Subscription callbacks may use context() freely: it is
 * session-stable, not per-frame.
 *
 * Header-only on purpose: behaviors compile into the hot-reloadable game
 * module, which resolves engine symbols from the host executable. Keeping
 * every Behavior method inline means the module carries its own copies and
 * never depends on which engine objects the host happened to link.
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
        /**
         * @brief Called on the first tick this instance runs in play mode.
         */
        virtual void onStart() {}

        /**
         * @brief Called every variable-step frame while play mode runs.
         *
         * @param dt Elapsed simulation time this frame, in seconds.
         */
        virtual void onUpdate(float dt) {}

        /**
         * @brief Called on each fixed-step tick (opt-in).
         *
         * Only invoked for behaviors that override it; left empty otherwise.
         *
         * @param dt Fixed timestep (fixedDeltaTime), in seconds.
         */
        virtual void onFixedUpdate(float dt) {}

        /**
         * @brief Called when a non-trigger contact with @p other occurs this tick.
         *
         * @param other The entity this one collided with.
         */
        virtual void onCollision(EntityId other) {}

        /**
         * @brief Called when this entity's trigger overlapped @p other this tick.
         *
         * @param other The entity that overlapped this trigger.
         */
        virtual void onTrigger(EntityId other) {}

        /**
         * @brief Called when this instance is torn down.
         *
         * Fires on entity removal, play stop, or engine shutdown.
         */
        virtual void onDestroy() {}

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
         * @brief The engine capability surface: scene, resources, window, events.
         *
         * Session-stable (owned by the BehaviorSystem), so it is safe to use
         * from subscribe() callbacks too, not just inside hooks. Valid from
         * just before onStart() until teardown.
         */
        BehaviorContext& context() { return *m_ctx; }

        /**
         * @brief Create a new (empty) entity; add components via context().scene.
         * Safe to call from a hook, with one caveat: attaching a ScriptComponent
         * to the new entity mid-hook can reallocate the behavior storage being
         * iterated - do that kind of structural script wiring outside the hot
         * loop (e.g. at scene setup), not inline.
         */
        Entity spawn() { return m_ctx->scene->createEntity(); }

        /**
         * @brief Destroy @p entity (and its subtree) - deferred until after the current
         * hook pass, so destroying your own entity is safe. Fires onDestroy on
         * the affected behaviors. Routed through HierarchyOperations.
         */
        void destroy(EntityId entity) { m_ctx->pendingDestroy->push_back(entity); }

        /**
         * @brief Subscribe to events of type EventT for this behavior's lifetime. The
         * subscription is dropped automatically when the behavior is destroyed
         * (or the play session ends), so there is nothing to clean up by hand.
         */
        template<typename EventT>
        void subscribe(std::function<void(const EventT&)> callback) {
            if (!m_ctx) return;
            EventBus* events = m_ctx->events;
            const ListenerId id = events->subscribe<EventT>(std::move(callback));
            m_subscriptions.push_back([events, id]() { events->unsubscribe<EventT>(id); });
        }

    private:
        friend class BehaviorSystem;

        /**
         * @brief Bind the entity identity and the engine capability surface.
         *
         * Called by BehaviorSystem before onStart. The context is the system's
         * own session-stable BehaviorContext, so one pointer covers everything
         * the accessors and helpers reach - growing the surface never changes
         * this signature.
         *
         * @param entity  The entity this behavior is attached to.
         * @param context The BehaviorSystem's stable capability bundle.
         */
        void bindContext(EntityId entity, BehaviorContext& context) {
            m_entity = entity;
            m_ctx    = &context;
        }

        /**
         * @brief Drop all subscribe<E>() listeners. Run from the destructor and, while
         * the EventBus is guaranteed alive, by BehaviorSystem::endSession at
         * play stop / shutdown (so it never unsubscribes from a dead bus).
         */
        void clearSubscriptions() {
            for (auto& unsubscribe : m_subscriptions) unsubscribe();
            m_subscriptions.clear();
        }

    protected:
        EntityId m_entity{};

    private:
        BehaviorContext* m_ctx = nullptr;

        std::vector<std::function<void()>> m_subscriptions;
        bool m_started  = false;
        bool m_disabled = false;
};

} // namespace Engine
