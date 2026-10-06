#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "logger.h"

#include "core/reflect.h"
#include "debug/engine_error_log.h"

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/hierarchy_operations.h"
#include "platform/window/window_manager.h"
#include "system/render/render_settings.h"
#include "core/event/event_bus.h"
#include "net/net_session.h"
#include "system/physics/physics_events.h"
#include "platform/input/input_map.h"

namespace Vkm::Engine {

class Behavior;
class Clock;
class ResourceManager;
class BehaviorSystem;
class BehaviorFieldVisitor;

/**
 * @brief Everything gameplay code may reach, bundled behind one pointer.
 *
 * Session-stable, unlike FrameContext, so subscribe() lambdas may keep using it.
 * A field belongs here exactly when behaviors are meant to use it.
 */
struct BehaviorContext {
    Scene*                 scene            = nullptr;
    ResourceManager*       resources        = nullptr;

    /// The only nullable field: a host that draws nothing has none.
    WindowManager*         window           = nullptr;

    EventBus*              events           = nullptr;
    InputMap*              input            = nullptr;
    NetSession*            net              = nullptr;
    Clock*                 clock            = nullptr;
    RenderSettings*        render           = nullptr;
    std::vector<EntityId>* pendingDestroy   = nullptr;
    std::string*           pendingSceneLoad = nullptr;
};

namespace detail {

/**
 * @brief The event type a listener takes, read off its call operator's parameter.
 *
 * @tparam Fn A non-generic lambda, a function object or a function pointer.
 */
template<typename Fn>
struct ListenerEvent : ListenerEvent<decltype(&Fn::operator())> {};

template<typename C, typename R, typename A>
struct ListenerEvent<R (C::*)(A) const> {
    using type = std::decay_t<A>;
};

template<typename C, typename R, typename A>
struct ListenerEvent<R (C::*)(A)> {
    using type = std::decay_t<A>;
};

template<typename R, typename A>
struct ListenerEvent<R (*)(A)> {
    using type = std::decay_t<A>;
};

/**
 * @brief The event Behavior::subscribe listens for: EventT when named, else the callback's parameter.
 *
 * A specialisation, not a conditional, so a named type never asks a generic lambda.
 *
 * @tparam EventT Type the caller named, or void.
 * @tparam Fn     Callback type.
 */
template<typename EventT, typename Fn>
struct SubscribedEvent {
    using type = EventT;
};

template<typename Fn>
struct SubscribedEvent<void, Fn> {
    using type = typename ListenerEvent<Fn>::type;
};

} // namespace detail

/**
 * @brief The first behavior on @p entity whose typeName() is @p typeName.
 *
 * @param scene    Scene holding the entity.
 * @param entity   Entity to look on; a dead one, or one with no ScriptComponent, has none.
 * @param typeName Registered behavior name.
 * @return The behavior, owned by the entity's ScriptComponent, or null.
 */
Behavior* findBehaviorNamed(Scene& scene, EntityId entity, std::string_view typeName);

/// @copydoc findBehaviorNamed(Scene&, EntityId, std::string_view)
const Behavior* findBehaviorNamed(const Scene& scene, EntityId entity, std::string_view typeName);

/**
 * @brief @p entity's behavior of type T, or null when it has none.
 *
 * Inside a behavior, Behavior::findBehavior says the same without the scene.
 *
 * @tparam T Behavior subclass with a VKM_REFLECT block.
 * @param scene  Scene holding the entity.
 * @param entity Entity to look on; a dead one has none.
 * @return The first T attached to @p entity, or nullptr.
 */
template<typename T>
T* findBehavior(Scene& scene, EntityId entity) {
    return static_cast<T*>(findBehaviorNamed(scene, entity, Reflect::Traits<T>::NAME));
}

/// @copydoc findBehavior(Scene&, EntityId)
template<typename T>
const T* findBehavior(const Scene& scene, EntityId entity) {
    return static_cast<const T*>(findBehaviorNamed(scene, entity, Reflect::Traits<T>::NAME));
}

/**
 * @brief Base class for native C++ gameplay behaviors.
 *
 * Attached through a ScriptComponent. The context is bound before onStart(), so no
 * accessor works in a constructor; being session-stable, they are safe from a
 * subscribe() callback and not worth caching.
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
         * @brief Called on the first simulation tick this instance runs in play mode.
         *
         * Never from onRealtimeUpdate, so one spawned while paused waits for time to flow.
         */
        virtual void onStart() {}

        /**
         * @brief Called every variable-step frame on which simulation time advanced.
         *
         * Skipped entirely while paused (and in editor Edit mode), never run with a zero
         * delta; work that must continue goes in onRealtimeUpdate.
         *
         * @param dt Elapsed simulation time this frame, in seconds; always > 0.
         */
        virtual void onUpdate(float dt) {}

        /**
         * @brief Called every frame on real time, paused or not.
         *
         * Only for started behaviors, so build a pause menu in onStart and toggle
         * UIElement::visible rather than spawning it on pause.
         *
         * @param dt Real frame delta in seconds, untouched by setTimeScale(); always > 0.
         */
        virtual void onRealtimeUpdate(float dt) {}

        /**
         * @brief Called on each fixed-step tick; fed from simulation time, so pause and time-scale apply.
         *
         * @param dt Fixed timestep (Clock::getFixedStep()), in seconds.
         */
        virtual void onFixedUpdate(float dt) {}

        /**
         * @brief Called on the first tick this entity touches another, in a resolved (non-trigger) contact.
         *
         * @code
         * void Crate::onCollisionEnter(const Collision& hit) {
         *     if (hit.normal.y > 0.7f) LOG_INFO("landed on %u", hit.other.slot());
         * }
         * @endcode
         *
         * @param hit Who, where, and the normal from them into this entity.
         */
        virtual void onCollisionEnter(const Collision& hit) {}

        /**
         * @brief Called on each later tick the contact lasts.
         *
         * Not while both bodies rest (asleep or static); waking does not enter again.
         *
         * @param hit The contact this tick.
         */
        virtual void onCollisionStay(const Collision& hit) {}

        /**
         * @brief Called on the first tick the contact is over.
         *
         * Including when the other was destroyed or disabled, so it may be dead. An
         * entity destroyed while touching hears nothing.
         *
         * @param hit Who it stopped touching; its point and normal are zero.
         */
        virtual void onCollisionExit(const Collision& hit) {}

        /**
         * @brief Called on the first tick @p other overlaps this entity's trigger.
         *
         * @param other The entity that entered.
         */
        virtual void onTriggerEnter(EntityId other) {}

        /**
         * @brief Called on each later tick @p other is still inside, on onCollisionStay's terms.
         *
         * @param other The entity still inside.
         */
        virtual void onTriggerStay(EntityId other) {}

        /**
         * @brief Called on the first tick @p other no longer overlaps, on onCollisionExit's terms.
         *
         * @param other The entity that left; it may no longer be alive.
         */
        virtual void onTriggerExit(EntityId other) {}

        /**
         * @brief Called on entity removal, and when the session ends (see BehaviorSystem::endSession).
         */
        virtual void onDestroy() {}

        /**
         * @brief Stable type name, identical to this type's BehaviorRegistry key; serialization uses it.
         *
         * @return The type's registered name.
         */
        virtual const char* typeName() const = 0;

        /**
         * @brief Visit the behavior's reflected authoring fields; ReflectedBehavior generates it.
         *
         * @param visitor Visited once per reflected field, in markup order.
         */
        virtual void visitFields(BehaviorFieldVisitor& visitor) {}

        /**
         * @brief Deep copy for entity duplication; authored fields only, the context is rebound.
         *
         * @return A new instance of the same type carrying the authored fields.
         */
        virtual std::unique_ptr<Behavior> clone() const = 0;

    protected:
        /**
         * @brief The entity this behavior is attached to.
         *
         * @return The owning entity; null until bound, just before onStart.
         */
        EntityId entity() const { return m_entity; }

        /**
         * @brief This entity's T, or null when it has none.
         *
         * @code
         * if (Transform* body = tryGet<Transform>()) body->position += step;
         * @endcode
         *
         * @tparam T Component type.
         * @return Pointer to this entity's T, or nullptr.
         */
        template<typename T>
        T* tryGet() { return m_ctx->scene->tryGet<T>(m_entity); }

        /// @copydoc tryGet()
        template<typename T>
        const T* tryGet() const { return m_ctx->scene->tryGet<T>(m_entity); }

        /**
         * @brief This entity's T, which it must have.
         *
         * @tparam T Component type; the entity must carry one.
         * @return Reference to this entity's T.
         */
        template<typename T>
        T& get() { return m_ctx->scene->get<T>(m_entity); }

        /// @copydoc get()
        template<typename T>
        const T& get() const { return m_ctx->scene->get<T>(m_entity); }

        /**
         * @brief Whether this entity carries a T.
         *
         * @tparam T Component type.
         * @return True when this entity has one.
         */
        template<typename T>
        bool has() const { return m_ctx->scene->has<T>(m_entity); }

        /**
         * @brief Give this entity a T.
         *
         * @tparam T Component type; the entity must not already have one.
         * @param component Component to store.
         * @return Reference to it in the scene's storage.
         */
        template<typename T>
        auto& add(T && component) {
            return m_ctx->scene->add(m_entity, std::forward<T>(component));
        }

        /**
         * @brief This entity's behavior of type T, or null when it has none.
         *
         * @tparam T Behavior subclass with a VKM_REFLECT block.
         * @return The first T attached to this entity, or nullptr.
         */
        template<typename T>
        T* findBehavior() { return findBehavior<T>(m_entity); }

        /**
         * @brief @p other's behavior of type T, or null when it has none.
         *
         * @code
         * if (Health* health = findBehavior<Health>(hit.other)) health->damage(10.0f);
         * @endcode
         *
         * Not worth caching: the entity may die and its slot be reused between hooks.
         *
         * @tparam T Behavior subclass with a VKM_REFLECT block.
         * @param other Entity to look on; a dead one has none.
         * @return The first T attached to @p other, or nullptr.
         */
        template<typename T>
        T* findBehavior(EntityId other) { return Vkm::Engine::findBehavior<T>(*m_ctx->scene, other); }

        /**
         * @brief The free findBehavior, which the member overloads would otherwise hide.
         *
         * @tparam T Behavior subclass with a VKM_REFLECT block.
         * @param world  Scene holding the entity.
         * @param entity Entity to look on; a dead one has none.
         * @return The first T attached to @p entity, or nullptr.
         */
        template<typename T>
        static T* findBehavior(Scene& world, EntityId entity) {
            return Vkm::Engine::findBehavior<T>(world, entity);
        }

        /// @copydoc findBehavior(Scene&, EntityId)
        template<typename T>
        static const T* findBehavior(const Scene& world, EntityId entity) {
            return Vkm::Engine::findBehavior<T>(world, entity);
        }

        /**
         * @brief The scene this behavior's entity lives in.
         *
         * @return The session's scene.
         */
        Scene& scene() { return *m_ctx->scene; }

        /**
         * @brief The assets this session holds, to look one up or add one.
         *
         * @return The session's resource manager.
         */
        ResourceManager& resources() { return *m_ctx->resources; }

        /**
         * @brief The event bus's sending half, to emit or enqueue on.
         *
         * Not the bus: a listener on it would outlive this instance; use subscribe().
         *
         * @return A sender over the session's bus.
         */
        EventSender events() { return EventSender(*m_ctx->events); }

        /**
         * @brief The quality settings the frame is drawn at; a write lands on the next frame.
         *
         * @return The session's render settings.
         */
        RenderSettings& render() { return *m_ctx->render; }

        /**
         * @brief Named input actions, for the frame queries and to define bindings.
         *
         * A fixed update reads command() instead: `input().pressed(command(), "Jump")`.
         *
         * @return The session's input map.
         */
        InputMap& input() { return *m_ctx->input; }

        /**
         * @brief The wire, or an offline session that answers as though local.
         *
         * @return The session's network session.
         */
        NetSession& net() { return *m_ctx->net; }

        /**
         * @brief Real and simulation time, and the play state behind them.
         *
         * A game may pause and resume through this. Never call beginFrame() or
         * consumeFixedStep(): they belong to the main loop.
         *
         * @return The session's clock.
         */
        Clock& clock() { return *m_ctx->clock; }
        const Clock& clock() const { return *m_ctx->clock; }

        /**
         * @brief The window, or null on a host that draws nothing, such as a dedicated server.
         *
         * @return The window, or nullptr.
         */
        WindowManager* window() { return m_ctx->window; }

        /**
         * @brief Whether this end decides what happens to this entity.
         *
         * True offline and on a server; on a client only for what it owns. Ask before
         * writing a Transform or velocity, never before drawing or playing a sound, or
         * a remote player goes silent and invisible.
         *
         * @return True when this end simulates the entity.
         */
        bool isSimulated() const {
            return m_ctx->net->simulates(m_entity);
        }

        /**
         * @brief Whether this entity belongs to the player at this end.
         *
         * Ask before touching the camera, mouse or HUD. Not isSimulated(): a server
         * simulates every player and owns none.
         *
         * @return True when the local player owns the entity.
         */
        bool isMine() const {
            return m_ctx->net->isMine(m_entity);
        }

        /**
         * @brief Whether this tick already happened and is being run again.
         *
         * A mispredicting client replays ticks. Computation must re-run, but nothing
         * presented (an animation, a sound) may. False offline and on a server.
         *
         * @return True during a replayed tick.
         */
        bool isReplaying() const { return m_ctx->net->replaying(); }

        /**
         * @brief The input driving this entity on the tick now running.
         *
         * Offline, the local player's, whatever the entity; online, its driving player's,
         * or nothing held. Read this, not the device, which misses taps between ticks,
         * repeats presses on slow frames and cannot be replayed.
         *
         * @return The command for this entity on this tick.
         */
        const InputCommand& command() const {
            return m_ctx->net->commandFor(m_entity);
        }

        /**
         * @brief Create a new, empty entity; add components to it through scene().
         *
         * Safe from a hook; a behavior added during a pass starts on the next. Local to
         * this end, so a client warns once; use NetSession::spawn on the server instead.
         *
         * @return The new entity.
         */
        EntityId spawn() {
            if (m_ctx->net->role() == NetRole::Client) warnSpawnOnClient();
            return m_ctx->scene->createEntity();
        }

        /**
         * @brief Create an entity carrying @p name.
         *
         * @param name Human-readable name; truncated into Name's fixed buffer.
         * @return The new entity.
         */
        EntityId spawn(const char* name) {
            const EntityId entity = spawn();
            m_ctx->scene->add(entity, makeName(name));
            return entity;
        }

        /**
         * @brief Create an entity carrying @p name, parented under @p parent.
         *
         * Parenting is HierarchyOperations::setParent.
         *
         * @param name Human-readable name; truncated into Name's fixed buffer.
         * @param parent Entity to attach it under; must be alive.
         * @return The new entity.
         */
        EntityId spawn(const char* name, EntityId parent) {
            const EntityId entity = spawn(name);
            HierarchyOperations::setParent(*m_ctx->scene, entity, parent);
            return entity;
        }

        /**
         * @brief Destroy @p entity and its subtree, firing onDestroy.
         *
         * Deferred past the hook pass, so destroying your own entity is safe; drained on
         * paused frames too.
         *
         * @param target Root of the subtree to destroy.
         */
        void destroy(EntityId target) { m_ctx->pendingDestroy->push_back(target); }

        /**
         * @brief Destroy this behavior's own entity and its subtree, deferred likewise.
         */
        void destroy() { destroy(m_entity); }

        /**
         * @brief Load @p scenePath, replacing everything currently in the scene.
         *
         * Deferred to the end of the hook pass, since it destroys the caller; the last
         * request wins. Works while paused, but the new scene starts only once time
         * flows, so a paused caller must resume the clock itself.
         *
         * @param scenePath Scene file, relative to the project root.
         */
        void loadScene(const std::string& scenePath) { *m_ctx->pendingSceneLoad = scenePath; }

        /**
         * @brief Listen for an event until the behavior is destroyed or the session ends.
         *
         * @code
         * subscribe([this](const UIClickEvent& click) { onClick(click); });
         * @endcode
         *
         * Name the type (`subscribe<UIClickEvent>(...)`) only for a generic lambda.
         *
         * @tparam EventT Event type to listen for; deduced when left out.
         * @tparam Fn Callable taking `const EventT&`.
         * @param callback Called with each event; a throw is reported but does not disable.
         */
        template<typename EventT = void, typename Fn>
        void subscribe(Fn&& callback) {
            using Event = typename detail::SubscribedEvent<EventT, std::decay_t<Fn>>::type;
            std::function<void(const Event&)> listener = std::forward<Fn>(callback);
            // Null only before binding, so this is a subscribe from a constructor.
            if (!m_ctx) {
                LOG_ERROR(
                    "A behavior subscribed before it started; "
                    "subscribe() belongs in onStart(), not the constructor"
                );
                return;
            }
            // Guarded, or a throw would unwind through the bus and out of the frame.
            EventBus* events = m_ctx->events;
            const ListenerId id = events->subscribe<Event>(
                [this, listener = std::move(listener)](const Event& event) {
                    runGuarded("event listener", [&] { listener(event); });
                }
            );
            m_subscriptions.push_back([events, id]() { events->unsubscribe<Event>(id); });
        }

    private:
        friend class BehaviorSystem;

        /**
         * @brief Run @p fn, reporting anything it throws against this behavior.
         *
         * @param hookName Named in the report beside the behavior's type.
         * @param fn       Gameplay call to make.
         * @return True if @p fn threw, leaving the consequence to the caller.
         */
        template<typename Fn>
        bool runGuarded(const char* hookName, Fn&& fn) {
            try {
                fn();
                return false;
            } catch (const std::exception& e) {
                reportError("Behavior", hookLabel(hookName), e.what());
            } catch (...) {
                reportError("Behavior", hookLabel(hookName), "non-std exception");
            }
            return true;
        }

        /**
         * @brief "Type / hook", built only on a throw so a guarded call never allocates.
         *
         * @param hookName Hook or callback that threw.
         * @return The error report's source label.
         */
        std::string hookLabel(const char* hookName) const {
            return std::string(typeName()) + " / " + hookName;
        }

        /**
         * @brief Bind the entity identity and the engine capability surface, before onStart.
         *
         * @param entity  Entity this behavior is attached to.
         * @param context The BehaviorSystem's stable capability bundle.
         */
        void bindContext(EntityId entity, BehaviorContext& context) {
            m_entity = entity;
            m_ctx    = &context;
        }

        /**
         * @brief Drop all subscribe() listeners.
         *
         * Run by BehaviorSystem::fireDestroy while the bus lives, so the destructor's
         * call never touches a dead bus.
         */
        void clearSubscriptions() {
            for (auto& unsubscribe : m_subscriptions) unsubscribe();
            m_subscriptions.clear();
        }

        /**
         * @brief Say, once per process, that a connected client made an entity of its own.
         *
         * Defined in vkm_core, or each module copy would hold its own latch.
         */
        static void warnSpawnOnClient();

    private:
        EntityId         m_entity{};
        BehaviorContext* m_ctx = nullptr;

        std::vector<std::function<void()>> m_subscriptions;
        bool m_started  = false;
        bool m_disabled = false;
};

} // namespace Vkm::Engine
