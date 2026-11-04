#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "vkm_log/logger.h"

#include "base/event/event_bus.h"
#include "core/graphics.h"
#include "base/math/ray.h"
#include "base/reflect/reflect.h"
#include "core/frame_context.h"
#include "debug/draw/debug_draw.h"
#include "debug/error/engine_error_log.h"
#include "ecs/entity.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "io/scene/prefab_ref.h"
#include "net/net_session.h"
#include "core/input_map.h"
#include "vkm_platform/window/window_manager.h"
#include "resource/asset_ref.h"
#include "core/frame/physics_events.h"
#include "system/physics/query.h"
#include "system/script/behavior_context.h"
#include "core/frame/screen_view.h"
#include "core/frame/ui_events.h"

namespace Vkm::Engine {

/**
 * @brief What an animation playAnimation starts does at its end.
 */
enum class AnimationEnd : uint8_t {
    Loop,  ///< Wraps to its start.
    Hold   ///< Stops on its last pose.
};

class BehaviorBase;
class BehaviorFieldVisitor;
class Clock;
class ResourceManager;
struct AnimationClipAsset;
struct AudioClipAsset;
struct Transform;

namespace detail {

/**
 * @brief The event type a listener takes, read off its call operator's parameter.
 *
 * @tparam Fn A non-generic lambda, a function object or a function pointer.
 */
template<typename Fn>
struct ListenerEvent {
    using type = typename ListenerEvent<decltype(&Fn::operator())>::type;
};

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
 * @brief The first behavior on @p entity whose typeName() is @p typeName.
 *
 * @param scene    Scene holding the entity.
 * @param entity   Entity to look on; a dead one, or one with no ScriptComponent, has none.
 * @param typeName Registered behavior name.
 * @return The behavior, owned by the entity's ScriptComponent, or null.
 */
BehaviorBase* findBehaviorNamed(Scene& scene, EntityId entity, std::string_view typeName);

/**
 * @brief The entity carrying a behavior named @p typeName with the lowest slot, the same at every end.
 *
 * @param scene    Scene to search.
 * @param typeName Registered behavior name.
 * @return The entity, or a null EntityId when nothing carries one.
 */
EntityId lowestWithBehaviorNamed(Scene& scene, std::string_view typeName);

/**
 * @brief Call @p fn with every behavior named @p typeName, and the entity carrying it, lowest slot
 *        first, so every end of a session meets them in the same order.
 *
 * @param scene    Scene to search.
 * @param typeName Registered behavior name.
 * @param fn       Called once per entity carrying one, with its first.
 */
void forEachBehaviorNamed(
    Scene& scene,
    std::string_view typeName,
    const std::function<void(EntityId, BehaviorBase&)>& fn
);

} // namespace detail

/**
 * @brief Base class for native C++ gameplay behaviors.
 *
 * Attached through a ScriptComponent. The context is bound before onStart(), so no
 * accessor works in a constructor; being session-stable, they are safe from a
 * subscribe() callback and not worth caching. docs/reference/scripting.md is the contract.
 */
class BehaviorBase {
    public:
        static constexpr float DEFAULT_FADE = 0.2f;  ///< playAnimation's cross-fade, in seconds.

    public:
        BehaviorBase() = default;
        virtual ~BehaviorBase() { clearSubscriptions(); }

        BehaviorBase(const BehaviorBase& other) = delete;
        BehaviorBase& operator=(const BehaviorBase& other) = delete;

        BehaviorBase(BehaviorBase && other) = delete;
        BehaviorBase& operator=(BehaviorBase && other) = delete;

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
         * Only for started behaviors, so build a pause menu at onStart, as a canvas that pauses
         * the game (UICanvas::pausesGame), rather than spawning it on pause.
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
         * @brief Stable type name, identical to this type's BehaviorRegistry key; scenes store it.
         *
         * @return The type's registered name.
         */
        virtual const char* typeName() const = 0;

        /**
         * @brief Visit the behavior's reflected authoring fields; Behavior<T> generates it.
         *
         * @param visitor Visited once per reflected field, in block order.
         */
        virtual void visitFields(BehaviorFieldVisitor& visitor) {}

        /**
         * @brief Deep copy for entity duplication; reflected fields only, the context is rebound.
         *
         * @return A new instance of the same type carrying the reflected fields.
         */
        virtual std::unique_ptr<BehaviorBase> clone() const = 0;

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
         * @brief This entity's T, or null when it has none: the one to reach for.
         *
         * @code
         * if (Transform* body = tryGet<Transform>()) body->position += step;
         * @endcode
         *
         * Like get, has, add and getOrAdd, a scene() call with this entity filled in; for another
         * entity call it on scene(), which takes a null id, so
         * `scene().tryGet<Transform>(findActiveCamera(scene()))` is safe.
         *
         * @tparam T Component type.
         * @return Pointer to this entity's T, or nullptr.
         */
        template<typename T>
        T* tryGet() { return frame().scene.tryGet<T>(m_entity); }

        /// @copydoc tryGet()
        template<typename T>
        const T* tryGet() const { return frame().scene.tryGet<T>(m_entity); }

        /**
         * @brief This entity's T, which it must have: one it cannot run without, such as one its own
         *        onStart added; tryGet otherwise.
         *
         * @tparam T Component type; the entity must carry one.
         * @return Reference to this entity's T.
         */
        template<typename T>
        T& get() { return frame().scene.get<T>(m_entity); }

        /// @copydoc get()
        template<typename T>
        const T& get() const { return frame().scene.get<T>(m_entity); }

        /**
         * @brief Whether this entity carries a T; scene().has with this entity filled in.
         *
         * @tparam T Component type.
         * @return True when this entity has one.
         */
        template<typename T>
        bool has() const { return frame().scene.has<T>(m_entity); }

        /**
         * @brief Give this entity a T.
         *
         * @tparam T Component type; the entity must not already have one.
         * @param component Component to store.
         * @return Reference to it in the scene's storage.
         */
        template<typename T>
        auto& add(T && component) {
            return frame().scene.add(m_entity, std::forward<T>(component));
        }

        /**
         * @brief This entity's T, given a default one first when it has none; scene().getOrAdd with
         *        this entity filled in.
         *
         * @tparam T Component type; default-constructible.
         * @return Reference to this entity's T.
         */
        template<typename T>
        T& getOrAdd() { return frame().scene.getOrAdd<T>(m_entity); }

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
         * @brief The entity of this one's subtree so named, itself included:
         *        HierarchyOperations::findNamed from this entity; findNamed for the whole scene.
         *
         * @param name The Name to look for, whole.
         * @return The entity so named, or a null id.
         */
        EntityId findPart(std::string_view name) const {
            return HierarchyOperations::findNamed(frame().scene, m_entity, name);
        }

        /**
         * @brief The entity of this one's subtree carrying a T, itself included; breadth first.
         *
         * @tparam T Component type.
         * @return The nearest entity carrying one, or a null id.
         */
        template<typename T>
        EntityId findPartWith() const {
            return HierarchyOperations::findInSelfOrDescendants<T>(frame().scene, m_entity);
        }

        /**
         * @brief This entity's behavior of type T, or null when it has none.
         *
         * @tparam T Behavior subclass with a VKM_BEHAVIOR block.
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
         * @tparam T Behavior subclass with a VKM_BEHAVIOR block.
         * @param other Entity to look on; a dead one has none.
         * @return The first T attached to @p other, or nullptr.
         */
        template<typename T>
        T* findBehavior(EntityId other) {
            return static_cast<T*>(detail::findBehaviorNamed(frame().scene, other, Reflect::Traits<T>::NAME));
        }

        /**
         * @brief The scene's T, for the behaviors a game has one of: how they find each other.
         *
         * A walk of every entity with a ScriptComponent, so look it up once, in onStart, and keep the
         * pointer, which holds while its entity lives. The lowest slot answers, so the choice is the
         * same on every machine.
         *
         * @tparam T Behavior subclass with a VKM_BEHAVIOR block.
         * @return The T, or nullptr when nothing carries one.
         */
        template<typename T>
        T* findInScene() {
            return findBehavior<T>(detail::lowestWithBehaviorNamed(frame().scene, Reflect::Traits<T>::NAME));
        }

        /**
         * @brief Call @p fn with every T in the scene and the entity carrying it, lowest slot first, so
         *        every end of a session meets them in the same order.
         *
         * @code
         * forEachBehavior<Switch>([&](EntityId id, Switch& button) { button.reset(); });
         * @endcode
         *
         * @tparam T  Behavior subclass with a VKM_BEHAVIOR block.
         * @tparam Fn Callable taking `(EntityId, T&)`.
         * @param fn  Called once per entity carrying a T, with its first.
         */
        template<typename T, typename Fn>
        void forEachBehavior(Fn&& fn) {
            const auto each = [&](EntityId id, BehaviorBase& behavior) { fn(id, static_cast<T&>(behavior)); };
            detail::forEachBehaviorNamed(frame().scene, Reflect::Traits<T>::NAME, each);
        }

        /**
         * @brief The scene this behavior's entity lives in, its water() included.
         *
         * @return The session's scene.
         */
        Scene& scene() { return frame().scene; }

        /**
         * @brief The assets this session holds, to look one up or add one.
         *
         * @return The session's resource manager.
         */
        ResourceManager& resources() { return frame().resources; }

        /**
         * @brief The event bus's sending half, to emit or enqueue on.
         *
         * Not the bus: a listener on it would outlive this instance; use subscribe().
         *
         * @return A sender over the session's bus.
         */
        EventSender events() { return EventSender(frame().events); }

        /**
         * @brief The render settings the frame is drawn with: settings(), set(), and a tier by
         *        setQuality().
         *
         * @return The session's Graphics.
         */
        Graphics& graphics() { return frame().graphics; }

        /**
         * @brief Whether @p action went down: in onFixedUpdate on this entity's command, so a tap
         *        between ticks lands once and a server reads its player's; else on this frame.
         *
         * @param action An action project.json lists; one it does not reads as never pressed.
         * @return True on the frame or tick it went down.
         */
        bool pressed(std::string_view action) const;

        /**
         * @brief Whether @p action is down, read as pressed() reads it.
         *
         * @param action An action project.json lists.
         * @return True while its value reaches 0.5 either way.
         */
        bool held(std::string_view action) const;

        /**
         * @brief Whether @p action came up, read as pressed() reads it.
         *
         * @param action An action project.json lists.
         * @return True on the frame or tick it came up.
         */
        bool released(std::string_view action) const;

        /**
         * @brief @p action's value, read as pressed() reads it.
         *
         * @param action An action project.json lists.
         * @return Its bindings' summed scales, clamped to -1..1; 0 for an unlisted action.
         */
        float axis(std::string_view action) const;

        /**
         * @brief A move across the ground that follows where the player looks: @p right and
         *        @p forward turned by the view the tick's command carries (InputCommand::view), level.
         *
         * @code
         * body->moveInput = alongView(axis("Move/Right"), axis("Move/Forward")) * runSpeed;
         * @endcode
         *
         * The view is the active camera's, so it needs no code of the game's. Looking straight down,
         * the top of the screen is ahead.
         *
         * @param right   How far right, -1..1: an axis() value.
         * @param forward How far ahead, -1..1.
         * @return World space, level, no longer than 1.
         */
        glm::vec3 alongView(float right, float forward) const;

        /**
         * @brief Named input actions, for the pointer, typed text, rebinding and what a command
         *        carries; pressed() and its kin already read the tick's command in onFixedUpdate.
         *
         * @return The session's input map.
         */
        InputMap& input() { return frame().input; }

        /**
         * @brief The wire, or an offline session that answers as though local.
         *
         * @return The session's network session.
         */
        NetSession& net() { return frame().net; }

        /**
         * @brief Real and simulation time, and the play state behind them.
         *
         * A game may pause and resume through this. Never call beginFrame() or
         * consumeFixedStep(): they belong to the main loop.
         *
         * @return The session's clock.
         */
        Clock& clock() { return frame().clock; }
        const Clock& clock() const { return frame().clock; }

        /**
         * @brief The window, or null on a host that draws nothing, such as a dedicated server.
         *
         * To aim the cursor into the world or pin a label over something, ask screen(): in the
         * editor's viewport the scene is not drawn over the whole window.
         *
         * @return The window, or nullptr.
         */
        Platform::WindowManager* window() { return frame().window.isOpen() ? &frame().window : nullptr; }

        /**
         * @brief Lines and shapes drawn into the world: `debugDraw().sphere(center, radius, color)`.
         *
         * Drawn in onFixedUpdate, one stands until the next tick's; given seconds, that long. See
         * DebugDraw.
         *
         * @return The host's, or one drawing nothing on a host that shows no frames and in a
         *         shipped game.
         */
        DebugDraw& debugDraw() { return frame().debug ? *frame().debug : DebugDraw::inert(); }

        /**
         * @brief The 3D view through the scene's active camera: the pointer's ray into the world, a
         *        point of the world on a canvas.
         *
         * Read off the host's rect for the scene, not the window, so it is right in the editor's
         * viewport too, where the scene is not drawn over the whole window.
         *
         * @return The view; not valid() on a host that draws nothing, or with no active camera.
         */
        ScreenView screen() const;

        /**
         * @brief The ray from the camera through the cursor.
         *
         * @return The ray; nothing when the cursor is off the view, or there is no view.
         */
        std::optional<Math::Ray> pointerRay() const;

        /**
         * @brief The nearest body along the ray within @p distance (raycast), this entity passed over.
         *
         * @param from      Where the ray starts, in world space.
         * @param direction Which way it points; raycast normalizes it.
         * @param distance  How far it reaches, in meters.
         * @param filter    Which bodies count; its ignore is this entity.
         * @return The hit, or nothing when the ray reaches no body.
         */
        std::optional<RayHit> cast(
            const glm::vec3& from,
            const glm::vec3& direction,
            float distance,
            QueryFilter filter = {}
        ) const;

        /**
         * @brief Every body overlapping the sphere, lowest slot first (Vkm::Engine::overlapSphere), this
         *        entity passed over.
         *
         * @param center Its center, in world space.
         * @param radius Its radius, in meters.
         * @param found  Filled with the bodies' entities.
         * @param filter Which bodies count; its ignore is this entity.
         */
        void overlapSphere(
            const glm::vec3& center,
            float radius,
            std::vector<EntityId>& found,
            QueryFilter filter = {}
        ) const;

        /**
         * @brief Whether this end decides what happens to this entity.
         *
         * True offline and on a server; on a client only for what it owns. Ask before
         * writing a Transform or velocity, never before drawing or playing a sound, or
         * a remote player goes silent and invisible.
         *
         * @return True when this end simulates the entity.
         */
        bool isSimulated() const { return frame().net.simulates(frame().scene, m_entity); }

        /**
         * @brief Whether this entity belongs to the player at this end.
         *
         * Ask before touching the camera, mouse or HUD. Not isSimulated(): a server
         * simulates every player and owns none.
         *
         * @return True when the local player owns the entity.
         */
        bool isMine() const { return frame().net.isMine(m_entity); }

        /**
         * @brief Whether this tick already happened and is being run again.
         *
         * A mispredicting client replays ticks. Computation must re-run, but nothing
         * presented (an animation, a sound) may. False offline and on a server.
         *
         * @return True during a replayed tick.
         */
        bool isReplaying() const { return frame().net.replaying(); }

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
         * What moves by the clock and must look alike at every end moves by these; Math::cycle() turns
         * them into a share of a period.
         *
         * @return Seconds of the authority's simulated time.
         */
        double worldTime() const;

        /**
         * @brief Whether the splash or the loading screen covers the world this frame.
         *
         * The world under a cover already runs, so a timer started in onStart spends what it shows
         * before the player sees any of it: count from the first frame this answers false. A tick
         * asks it of the frame before, as the cover is worked out after the ticks.
         *
         * @return True while either shows; false in a host that shows neither.
         */
        bool isCovered() const { return m_ctx->covered; }

        /**
         * @brief The input driving this entity on the tick now running.
         *
         * Offline, the local player's, whatever the entity; online, its driving player's,
         * or nothing held. pressed() and its kin read it in onFixedUpdate.
         *
         * @return The command for this entity on this tick.
         */
        const InputCommand& command() const { return frame().net.commandFor(m_entity); }

        /**
         * @brief Create an entity carrying @p name, under @p parent when one is given.
         *
         * Safe from a hook; a behavior added during a pass starts on the next. This end's
         * own: on a server, NetSession::spawn is what every player is told about. Parenting is
         * HierarchyOperations::setParent.
         *
         * @param name   Human-readable name; truncated into Name's fixed buffer.
         * @param parent Entity to attach it under, alive; null leaves it a root.
         * @return The new entity.
         */
        EntityId spawn(std::string_view name, EntityId parent = {});

        /**
         * @brief Build the prefab a PrefabRef field names here, at this end alone: an effect, a
         *        decoration.
         *
         * Prefab::instantiate, against this session's scene and assets.
         *
         * @param prefab The field.
         * @param at     Root pose.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId instantiate(const PrefabRef& prefab, const Transform& at);

        /**
         * @brief Build the prefab a PrefabRef field names here and on every client: NetSession::spawn.
         *
         * Authority only (isAuthority()): refused on a client.
         *
         * @param prefab The field.
         * @param at     Root pose.
         * @return The instance root, or a null id when nothing was built.
         */
        EntityId instantiateShared(const PrefabRef& prefab, const Transform& at);

        /**
         * @brief Play @p clip once from @p at: a PlaySoundEvent, sent now.
         *
         * Silent in a replayed tick (isReplaying()), which re-runs a moment already presented.
         *
         * @param clip   The clip; one not loaded, or none named, is heard as nothing.
         * @param at     World position it sounds from.
         * @param volume Linear gain.
         * @param spread Each play's pitch and volume vary by up to this share either way, so a sound
         *               heard often does not repeat exactly: 0.1 is plus or minus a tenth.
         */
        void playSoundAt(
            const AssetRef<AudioClipAsset>& clip,
            const glm::vec3& at,
            float volume = 1.0f,
            float spread = 0.0f
        ) {
            sendSound(clip, at, volume, spread, {});
        }

        /**
         * @brief Play @p clip once from where this entity is (worldPosition()), which its own body does
         *        not muffle (PlaySoundEvent::from).
         *
         * @param clip   The clip; one not loaded, or none named, is heard as nothing.
         * @param volume Linear gain.
         * @param spread As playSoundAt's.
         */
        void playSound(const AssetRef<AudioClipAsset>& clip, float volume = 1.0f, float spread = 0.0f) {
            sendSound(clip, worldPosition(), volume, spread, entity());
        }

        /**
         * @brief Destroy @p target and its subtree, firing onDestroy.
         *
         * Deferred past the hook pass, so destroying your own entity is safe; drained on
         * paused frames too.
         *
         * @param target Root of the subtree to destroy.
         */
        void destroy(EntityId target) { m_ctx->pendingDestroy.push_back(target); }

        /**
         * @brief Destroy this behavior's own entity and its subtree, deferred likewise.
         */
        void destroy() { destroy(m_entity); }

        /**
         * @brief Destroy @p target and its subtree once @p seconds of live ticks have run; nothing in a
         *        replayed tick.
         *
         * Counted on the fixed step, never in a replayed tick, and ended with the session.
         *
         * @param target  Root of the subtree to destroy.
         * @param seconds How long from now; a second call for @p target replaces the time.
         */
        void destroyAfter(EntityId target, float seconds);

        /**
         * @brief Destroy this behavior's own entity once @p seconds of live ticks have run.
         *
         * @param seconds How long from now; a second call replaces the time.
         */
        void destroyAfter(float seconds) { destroyAfter(m_entity, seconds); }

        /**
         * @brief Call @p fn once @p seconds of live ticks from now, unless this behavior goes first;
         *        nothing in a replayed tick.
         *
         * @param seconds How long from now, counted on the fixed step.
         * @param fn      Called once, guarded as a hook is.
         */
        template<typename Fn>
        void after(float seconds, Fn&& fn) {
            if (isReplaying()) return;  // the live tick asked already
            m_after.push_back(Delayed{seconds, std::function<void()>(std::forward<Fn>(fn))});
        }

        /**
         * @brief Cross-fade the nearest Animator of this entity's subtree to @p clip; nothing in a
         *        replayed tick.
         *
         * @param clip        The clip; one not loaded, or none named, is refused.
         * @param fadeSeconds How long the old pose takes to fade out.
         * @param end         Whether the clip loops or holds its last pose.
         * @return Whether an Animator took the clip: false for a replay, no Animator or no clip.
         */
        bool playAnimation(
            const AssetRef<AnimationClipAsset>& clip,
            float fadeSeconds = DEFAULT_FADE,
            AnimationEnd end = AnimationEnd::Loop
        );

        /**
         * @brief The lowest-slot entity of the whole scene carrying this Name; findPart for a part of
         *        this entity.
         *
         * @param name The Name to look for, whole.
         * @return The entity so named, or a null id.
         */
        EntityId findNamed(std::string_view name) const;

        /**
         * @brief Load @p scenePath, replacing everything currently in the scene.
         *
         * Deferred to the end of the hook pass, since it destroys the caller; the last
         * request wins. Under a host's loading screen it waits for the cover. Works while paused,
         * and a pausing canvas goes with the old world; but the new scene starts only once time
         * flows, so a caller that paused the clock itself resumes it.
         *
         * @param scenePath Scene file, relative to the project root.
         */
        void loadScene(const std::string& scenePath) { m_ctx->pendingSceneLoad = scenePath; }

        /**
         * @brief Start the scene being played over: the file loadScene last loaded, else the scene
         *        the host began play with - its entry scene, or under the editor the scene as Play
         *        found it, unsaved edits and all.
         *
         * Deferred as loadScene is; a loadScene asked in the same pass wins.
         */
        void restartScene() { m_ctx->pendingRestart = true; }

        /**
         * @brief Listen for the event @p callback takes until this behavior is destroyed or the
         *        session ends.
         *
         * @code
         * subscribe([this](const UIClickEvent& click) { onClick(click); });
         * @endcode
         *
         * @tparam Fn A non-generic callable: its parameter names the event.
         * @param callback A lambda taking `const Event&`; a throw is reported but does not disable.
         */
        template<typename Fn>
        void subscribe(Fn&& callback) {
            using Event = typename detail::ListenerEvent<std::decay_t<Fn>>::type;
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
            EventBus* events    = &frame().events;
            const ListenerId id = events->subscribe<Event>(
                [this, listener = std::move(listener)](const Event& event) {
                    runGuarded("event listener", [&] { listener(event); });
                }
            );
            m_subscriptions.push_back([events, id]() { events->unsubscribe<Event>(id); });
        }

        /**
         * @brief Call @p fn() at each click of @p button (UI::button), until this behavior goes.
         *
         * @param button The button's entity.
         * @param fn     Called with no arguments.
         */
        template<typename Fn>
        void onClick(EntityId button, Fn&& fn) {
            subscribe([button, fn = std::forward<Fn>(fn)](const UIClickEvent& click) mutable {
                if (click.entity == button) fn();
            });
        }

        /**
         * @brief Call @p fn(value) at each change of @p control, until this behavior goes: a
         *        slider's value, a toggle's 1 or 0, a choice's index.
         *
         * @param control The control's entity.
         * @param fn      Called with the new value, a float.
         */
        template<typename Fn>
        void onChange(EntityId control, Fn&& fn) {
            subscribe([control, fn = std::forward<Fn>(fn)](const UIValueEvent& change) mutable {
                if (change.entity == control) fn(change.value);
            });
        }

    private:
        friend class BehaviorSystem;

        /**
         * @brief playSoundAt and playSound's request, sent now unless the tick is a replay.
         *
         * @param clip   The clip; one not loaded, or none named, is heard as nothing.
         * @param at     World position it sounds from.
         * @param volume Linear gain.
         * @param spread playSoundAt's.
         * @param from   PlaySoundEvent::from.
         */
        void sendSound(
            const AssetRef<AudioClipAsset>& clip,
            const glm::vec3& at,
            float volume,
            float spread,
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
         * @brief The session's frame, which every accessor reads.
         *
         * @return The frame BehaviorSystem::init gave the context.
         */
        FrameContext& frame() const { return *m_ctx->frame; }

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
         * @brief Bind the entity and the context, before onStart.
         *
         * @param entity  Entity this behavior is attached to.
         * @param context The BehaviorSystem's stable context for the session.
         */
        void bindContext(EntityId entity, BehaviorContext& context) {
            m_entity = entity;
            m_ctx    = &context;
        }

        /**
         * @brief Drop every subscribe() listener and after() call.
         *
         * Run by BehaviorSystem::fireDestroy while the bus lives, so the destructor's
         * call never touches a dead bus.
         */
        void clearSubscriptions() {
            for (auto& unsubscribe : m_subscriptions) unsubscribe();
            m_subscriptions.clear();
            m_after.clear();
        }

    private:
        /**
         * @brief A call after() asked for, and the live seconds it still waits.
         */
        struct Delayed {
            float                 seconds = 0.0f;
            std::function<void()> fn;
        };

        EntityId         m_entity{};
        BehaviorContext* m_ctx = nullptr;

        std::vector<Delayed> m_after;

        std::vector<std::function<void()>> m_subscriptions;
        bool                               m_started  = false;
        bool                               m_disabled = false;
};

} // namespace Vkm::Engine
