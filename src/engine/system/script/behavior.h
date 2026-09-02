#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "platform/window/window_manager.h"
#include "core/event/event_bus.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"

namespace Vkm::Engine {

class Clock;
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
 * when behaviors are meant to use it. The Clock qualifies because
 * onRealtimeUpdate outlives a pause: a behavior may pause, resume, scale time
 * and read any delta, and still be ticking afterwards to undo it. What it must
 * not call is beginFrame() or consumeFixedStep() - those belong to the main
 * loop, and driving them from a hook corrupts the frame the hook is in.
 */
struct BehaviorContext {
    Scene*                 scene            = nullptr;
    ResourceManager*       resources        = nullptr;

    /// The only field here that can be null: a host that draws nothing has none.
    WindowManager*         window           = nullptr;

    EventBus*              events           = nullptr;
    InputMap*              input            = nullptr;
    NetSession*            net              = nullptr;
    Clock*                 clock            = nullptr;
    std::vector<EntityId>* pendingDestroy   = nullptr;
    std::string*           pendingSceneLoad = nullptr;
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
 * Header-only, and nothing forces it: every method below is a one-line
 * forwarder over the context and subscribe() is a template, so a .cpp would
 * hold nothing.
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
         * Always a simulation tick: onRealtimeUpdate never starts a behavior, so
         * an entity spawned while paused waits for time to flow again, and a
         * scene merely sitting open in the editor runs nothing at all.
         */
        virtual void onStart() {}

        /**
         * @brief Called every variable-step frame on which simulation time advanced.
         *
         * Simulation time is the timeline gameplay lives on. While the game is
         * paused - which in the editor is also Edit mode - this does not run at
         * all, rather than run with a zero delta, so a per-frame counter or an
         * input edge written here cannot tick while the world is frozen. Work
         * that must continue through a pause goes in onRealtimeUpdate.
         *
         * @param dt Elapsed simulation time this frame, in seconds; always > 0.
         */
        virtual void onUpdate(float dt) {}

        /**
         * @brief Called every frame on real time, paused or not.
         *
         * The hook for what a frozen world must not freeze: a pause menu's
         * animation, an unscaled timer, ducking the music, holding a key to
         * quit. @p dt is the real frame delta, so setTimeScale() does not reach
         * it either.
         *
         * Only behaviors that have already started receive it, and it starts
         * none itself - so a pause menu is built in onStart and shown by
         * toggling UIElement::visible, not spawned when the pause happens.
         *
         * @param dt Elapsed real time this frame, in seconds; always > 0.
         */
        virtual void onRealtimeUpdate(float dt) {}

        /**
         * @brief Called on each fixed-step tick (opt-in).
         *
         * Only invoked for behaviors that override it; left empty otherwise.
         * The accumulator behind it is fed from simulation time, so pause and
         * time-scale reach these steps with no gate of their own.
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
         * @brief Whether this end decides what happens to this entity.
         *
         * True for everything in a single-player game and on a server, and on a
         * client only for what that client owns. A behavior that moves its
         * entity - a controller, a mover, anything that writes a Transform or a
         * velocity - asks this first and returns when the answer is no, or it
         * is guessing at a body it will be corrected on every snapshot.
         *
         * Reading state, drawing, playing a sound: those run everywhere, and
         * asking this would make a remote player silent and invisible.
         */
        bool isSimulated() const {
            return m_ctx->net->simulates(m_entity);
        }

        /**
         * @brief Whether this entity belongs to the player at this end.
         *
         * What to ask before touching anything that is about *this* player -
         * the camera, the mouse, the heads-up display. Not the same question as
         * isSimulated(): a server simulates every player and owns none of them.
         */
        bool isMine() const {
            return m_ctx->net->isMine(m_entity);
        }

        /**
         * @brief Whether this tick already happened and is being run again.
         *
         * A client that predicted a tick wrongly re-runs every tick since from
         * the server's answer. What a behavior computes must re-run - that is
         * what a replay is for - but anything it *presents* must not: an
         * animation chosen again is a clip restarted, a sound played again is a
         * sound heard twice, and a replayed tick can choose differently from
         * the live one because it is simulating from a different state.
         *
         * The engine draws the same line for systems, in System::isReplayed().
         * This is that line one level down, for the code the engine cannot see.
         *
         * False offline and on a server, which never replay.
         */
        bool isReplaying() const { return m_ctx->net->replaying(); }

        /**
         * @brief The input driving this entity on the tick now running.
         *
         * The same call in all three roles, which is the point: offline and on
         * the owning client it is the local player's command, and on a server
         * it is what that entity's player sent, run on the tick they sent it
         * for. An entity no player drives reads as nothing held.
         *
         * Read this rather than the device. A fixed update that asks the device
         * misses a tap that began and ended between two ticks, repeats a press
         * on every tick of a slow frame, and cannot be replayed - and a
         * command that cannot be replayed cannot be predicted.
         */
        const InputCommand& command() const {
            return m_ctx->net->commandFor(m_entity);
        }

        /**
         * @brief Create a new (empty) entity; add components via context().scene.
         *
         * Safe from a hook, including attaching a ScriptComponent to the new
         * entity: BehaviorSystem walks a snapshot and re-resolves each entity,
         * so growing the component storage mid-pass moves it underneath the
         * loop without consequence. A behavior added during a pass starts on
         * the next one - the same rule destroy() follows in the other
         * direction.
         */
        EntityId spawn() { return m_ctx->scene->createEntity(); }

        /**
         * @brief Destroy @p entity and its subtree.
         *
         * Deferred until after the current hook pass, so destroying your own
         * entity is safe, and drained on a paused frame too, so
         * onRealtimeUpdate may use it. Routed through HierarchyOperations, and
         * fires onDestroy on the affected behaviors.
         */
        void destroy(EntityId entity) { m_ctx->pendingDestroy->push_back(entity); }

        /**
         * @brief Load @p scenePath, replacing everything currently in the scene.
         *
         * Deferred to the end of the hook pass, and it has to be: the load
         * destroys every entity including the one whose behavior asked for it,
         * so doing it inline would free this object mid-call. Requesting twice
         * in one pass keeps the last request - the scene can only become one
         * thing. The drain runs on paused frames as well, so a pause menu's
         * "quit to the main menu" works while the world is frozen - but nothing
         * in the scene that arrives starts until simulation time flows, and
         * this behavior is destroyed by the load, so a paused caller resumes
         * the clock itself or loads a world that can never run.
         *
         * @param scenePath Scene file, relative to the project root.
         */
        void loadScene(const std::string& scenePath) { *m_ctx->pendingSceneLoad = scenePath; }

        /**
         * @brief Subscribe to events of type EventT for this behavior's lifetime.
         *
         * The subscription is dropped automatically when the behavior is
         * destroyed, or when the play session ends, so there is nothing to
         * clean up by hand.
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
         * the accessors and helpers reach.
         *
         * @param entity  The entity this behavior is attached to.
         * @param context The BehaviorSystem's stable capability bundle.
         */
        void bindContext(EntityId entity, BehaviorContext& context) {
            m_entity = entity;
            m_ctx    = &context;
        }

        /**
         * @brief Drop all subscribe<E>() listeners.
         *
         * Run from the destructor and, while the EventBus is guaranteed alive,
         * by BehaviorSystem::endSession at play stop and shutdown, so it never
         * unsubscribes from a dead bus.
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

} // namespace Vkm::Engine
