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
#include "debug/debug_draw.h"
#include "debug/engine_error_log.h"

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab_ref.h"
#include "resource/asset_ref.h"
#include "platform/window/window_manager.h"
#include "system/render/render_settings.h"
#include "core/event/event_bus.h"
#include "net/net_session.h"
#include "system/physics/physics_events.h"
#include "platform/input/input_map.h"
#include "system/ui/screen_view.h"

namespace Vkm::Engine {

class Behavior;
class HostChrome;
class Clock;
class ResourceManager;
class BehaviorSystem;
class BehaviorFieldVisitor;
struct AudioClipAsset;
struct OceanData;
struct Transform;
class WaterData;

/**
 * @brief Everything gameplay code may reach, bundled behind one pointer.
 *
 * Session-stable, unlike FrameContext, so subscribe() lambdas may keep using it - but for
 * `ocean` and `water`, frame products BehaviorSystem refreshes each tick and frame. A field belongs here
 * exactly when behaviors are meant to use it.
 */
struct BehaviorContext {
    Scene*                 scene            = nullptr;
    ResourceManager*       resources        = nullptr;

    /// Null in a host that draws nothing.
    WindowManager*         window           = nullptr;

    EventBus*              events           = nullptr;
    InputMap*              input            = nullptr;
    NetSession*            net              = nullptr;
    Clock*                 clock            = nullptr;
    RenderSettings*        render           = nullptr;
    const RenderSettings*  projectRender    = nullptr;
    const HostChrome*      chrome           = nullptr;  ///< Where in the window the scene is drawn.
    std::vector<EntityId>* pendingDestroy   = nullptr;
    std::string*           pendingSceneLoad = nullptr;
    bool*                  pendingRestart   = nullptr;
    const OceanData*       ocean            = nullptr;  ///< OceanSystem's, this tick's or frame's.
    const WaterData*       water            = nullptr;  ///< WaterSystem's, this tick's or frame's.
    DebugDraw*             debug            = nullptr;  ///< Null with no frames shown, or shipped.
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
 * @brief The entity carrying a behavior named @p typeName with the lowest slot.
 *
 * @param scene    Scene to search.
 * @param typeName Registered behavior name.
 * @return The entity, or a null EntityId when nothing carries one.
 */
EntityId findEntityWithBehaviorNamed(Scene& scene, std::string_view typeName);

/**
 * @brief Call @p fn with every behavior named @p typeName, and the entity carrying it, lowest slot
 *        first.
 *
 * @param scene    Scene to search.
 * @param typeName Registered behavior name.
 * @param fn       Called once per entity carrying one, with its first.
 */
void forEachBehaviorNamed(
    Scene& scene,
    std::string_view typeName,
    const std::function<void(EntityId, Behavior&)>& fn
);

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
 * @brief The entity carrying a T, for the behaviors a game has one of.
 *
 * The lowest slot answers, so the choice is the same on every machine.
 *
 * @tparam T Behavior subclass with a VKM_REFLECT block.
 * @param scene Scene to search.
 * @return The entity, or a null EntityId when nothing carries one.
 */
template<typename T>
EntityId findEntityWithBehavior(Scene& scene) {
    return findEntityWithBehaviorNamed(scene, Reflect::Traits<T>::NAME);
}

/**
 * @brief The scene's T, for the behaviors a game has one of: how they find each other.
 *
 * A walk of every entity with a ScriptComponent, so look it up once - in onStart - and keep the
 * pointer, which holds while its entity lives.
 *
 * The lowest slot answers, as findEntityWithBehavior.
 *
 * @tparam T Behavior subclass with a VKM_REFLECT block.
 * @param scene Scene to search.
 * @return The T, or nullptr when nothing carries one.
 */
template<typename T>
T* findInScene(Scene& scene) {
    return findBehavior<T>(scene, findEntityWithBehavior<T>(scene));
}

/**
 * @brief Call @p fn with every T in the scene and the entity carrying it.
 *
 * A walk of every entity with a ScriptComponent, lowest slot first, so every end of a session
 * meets them in the same order.
 *
 * @tparam T  Behavior subclass with a VKM_REFLECT block.
 * @tparam Fn Callable taking `(EntityId, T&)`.
 * @param scene Scene to search.
 * @param fn    Called once per entity carrying a T, with its first.
 */
template<typename T, typename Fn>
void forEachBehavior(Scene& scene, Fn&& fn) {
    forEachBehaviorNamed(scene, Reflect::Traits<T>::NAME, [&](EntityId id, Behavior& behavior) {
        fn(id, static_cast<T&>(behavior));
    });
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
         * @param hit Who it stopped touching; its point, normal and speed are zero.
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

        /**
         * @brief The entity this behavior is attached to.
         *
         * Public: a behavior handed another finds where it is by asking it.
         *
         * @return The owning entity; null until bound, just before onStart.
         */
        EntityId entity() const { return m_entity; }

    protected:

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
         * @brief This entity's T, given a default one first when it has none.
         *
         * @tparam T Component type; default-constructible.
         * @return Reference to this entity's T.
         */
        template<typename T>
        T& getOrAdd() { return m_ctx->scene->getOrAdd<T>(m_entity); }

        /**
         * @brief Where this entity is in the world now, a move made this tick included.
         *
         * @return Its world position; the origin when it has no Transform.
         */
        glm::vec3 worldPosition() const;

        /**
         * @brief How this entity is turned in the world now, a turn made this tick included.
         *
         * @return Its world rotation; none (identity) when it has no Transform.
         */
        glm::quat worldRotation() const;

        /**
         * @brief HierarchyOperations::findNamed from this entity.
         *
         * @param name The Name to look for, whole.
         * @return The entity so named, or a null id.
         */
        EntityId findPart(std::string_view name) const {
            return HierarchyOperations::findNamed(*m_ctx->scene, m_entity, name);
        }

        /**
         * @brief The entity of this one's subtree carrying a T, itself included; breadth first.
         *
         * @tparam T Component type.
         * @return The nearest entity carrying one, or a null id.
         */
        template<typename T>
        EntityId findPartWith() const {
            return HierarchyOperations::findInSelfOrDescendants<T>(*m_ctx->scene, m_entity);
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
         * @brief The scene's behavior of type T, as findInScene(Scene&) finds it.
         *
         * @tparam T The behavior type sought.
         * @return The lowest-slot entity's, or null when none carries one.
         */
        template<typename T>
        T* findInScene() { return ::Vkm::Engine::findInScene<T>(*m_ctx->scene); }

        /**
         * @brief Call @p fn with every T in the scene, as forEachBehavior(Scene&, Fn&&) does.
         *
         * @code
         * forEachBehavior<Switch>([&](EntityId id, Switch& button) { button.reset(); });
         * @endcode
         *
         * @tparam T  Behavior subclass with a VKM_REFLECT block.
         * @tparam Fn Callable taking `(EntityId, T&)`.
         * @param fn  Called once per entity carrying a T.
         */
        template<typename T, typename Fn>
        void forEachBehavior(Fn&& fn) {
            ::Vkm::Engine::forEachBehavior<T>(*m_ctx->scene, std::forward<Fn>(fn));
        }

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
         * @brief The project's render settings, which render() started from.
         *
         * What a settings screen builds a quality tier over - withPreset(projectRender(), tier) -
         * so choosing High after Low brings back what Low turned off.
         *
         * @return The project's render settings.
         */
        const RenderSettings& projectRender() const { return *m_ctx->projectRender; }

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
         * @brief Lines and shapes drawn into the world: `debugDraw().sphere(centre, radius, color)`.
         *
         * Drawn in onFixedUpdate, one stands until the next tick's; given seconds, that long. See
         * DebugDraw.
         *
         * @return The host's, or one drawing nothing on a host that shows no frames and in a
         *         shipped game.
         */
        DebugDraw& debugDraw() { return m_ctx->debug ? *m_ctx->debug : DebugDraw::inert(); }

        /**
         * @brief The 3D view through the scene's active camera: the pointer's ray into the world, a
         *        point of the world on a canvas.
         *
         * Read off the host's rect for the scene, not the window, so it is right in the editor's
         * Play panel too.
         *
         * @return The view; not valid() on a host that draws nothing, or with no active camera.
         */
        ScreenView screen() const;

        /**
         * @brief The scene's sea at this moment: the tick's in onFixedUpdate, the frame's in
         *        onUpdate (OceanSystem).
         *
         * What a float rides - `surface.sampleAt(point)` is the water over a point of the plane,
         * above `level`, where it rests - and what the renderer draws, the same at every end.
         *
         * @return The sea, or null when the scene has no ocean.
         */
        const OceanData* ocean() const;

        /**
         * @brief The scene's water at this moment, the sea's and every other's: the tick's in
         *        onFixedUpdate, the frame's in onUpdate (WaterSystem).
         *
         * `at(point)` is the water over or under a world point - its surface, normal and flow. A
         * Buoyancy component floats a body on it without a behavior.
         *
         * @return The water; none at all on a host that runs no WaterSystem.
         */
        const WaterData& water() const;

        /**
         * @brief The ray from the camera through the cursor.
         *
         * @return The ray; nothing when the cursor is off the view, or there is no view.
         */
        std::optional<Math::Ray> pointerRay() const;

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
            return m_ctx->net->simulates(*m_ctx->scene, m_entity);
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
         * @brief Whether this end is the authority: offline, or a server.
         *
         * Where what is shared is decided - a score, a door's state - and where instantiateShared
         * may be called. Not isSimulated(), which a client answers for what it owns.
         *
         * @return True offline and on a server.
         */
        bool isAuthority() const;

        /**
         * @brief The seconds of the authority's world this end draws, NetSession::worldTime's.
         *
         * What moves by the clock and must look alike at every end moves by these; cycle() turns
         * them into a share of a period.
         *
         * @return Seconds of the authority's simulated time.
         */
        double worldTime() const;

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
         * Safe from a hook; a behavior added during a pass starts on the next. This end's
         * own: on a server, NetSession::spawn is what every player is told about.
         *
         * @return The new entity.
         */
        EntityId spawn() {
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
         * @brief Build the prefab at @p prefab here, at this end alone: an effect, a decoration.
         *
         * Prefab::instantiate, against this session's scene and assets.
         *
         * @param prefab Project-relative path.
         * @param at     Root pose.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId instantiate(const std::string& prefab, const Transform& at);

        /**
         * @brief Build the prefab a PrefabRef field names here, at this end alone.
         *
         * @param prefab The field.
         * @param at     Root pose.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId instantiate(const PrefabRef& prefab, const Transform& at) {
            return instantiate(prefab.path, at);
        }

        /**
         * @brief Build the prefab at @p prefab here and on every client: NetSession::spawn.
         *
         * Authority only (isAuthority()): refused on a client.
         *
         * @param prefab Project-relative path.
         * @param at     Root pose.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId instantiateShared(const std::string& prefab, const Transform& at);

        /**
         * @brief Build the prefab a PrefabRef field names here and on every client; authority only.
         *
         * @param prefab The field.
         * @param at     Root pose.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId instantiateShared(const PrefabRef& prefab, const Transform& at) {
            return instantiateShared(prefab.path, at);
        }

        /**
         * @brief Play @p clip once from @p at: a PlaySoundEvent, sent now.
         *
         * Silent in a replayed tick (isReplaying()), which re-runs a moment already presented.
         *
         * @param clip   The clip; one not loaded, or none named, is heard as nothing.
         * @param at     World position it sounds from.
         * @param volume Linear gain.
         */
        void playSoundAt(const AssetRef<AudioClipAsset>& clip, const glm::vec3& at, float volume = 1.0f) {
            sendSound(clip, at, volume, {});
        }

        /**
         * @brief Play @p clip once from where this entity is (worldPosition()), which its own body does
         *        not muffle (PlaySoundEvent::from).
         *
         * @param clip   The clip; one not loaded, or none named, is heard as nothing.
         * @param volume Linear gain.
         */
        void playSound(const AssetRef<AudioClipAsset>& clip, float volume = 1.0f) {
            sendSound(clip, worldPosition(), volume, entity());
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
         * @brief Start the scene being played over: the file loadScene last loaded, else the scene
         *        the host began play with - its entry scene, or under the editor the scene as Play
         *        found it, unsaved edits and all.
         *
         * Deferred as loadScene is; a loadScene asked in the same pass wins.
         */
        void restartScene() { *m_ctx->pendingRestart = true; }

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
         * @brief playSoundAt and playSound's request, sent now unless the tick is a replay.
         *
         * @param clip   The clip; one not loaded, or none named, is heard as nothing.
         * @param at     World position it sounds from.
         * @param volume Linear gain.
         * @param from   PlaySoundEvent::from.
         */
        void sendSound(
            const AssetRef<AudioClipAsset>& clip,
            const glm::vec3& at,
            float volume,
            EntityId from
        );

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

    private:
        EntityId         m_entity{};
        BehaviorContext* m_ctx = nullptr;

        std::vector<std::function<void()>> m_subscriptions;
        bool m_started  = false;
        bool m_disabled = false;
};

} // namespace Vkm::Engine
