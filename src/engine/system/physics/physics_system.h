#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "vkm_physics/body.h"
#include "vkm_physics/joint.h"
#include "vkm_physics/part.h"
#include "vkm_physics/stepper.h"

#include "core/system.h"
#include "debug/error/fault_latch.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class NetSession;

struct PhysicsSettings;

class EventBus;
class ResourceManager;
class Scene;

struct Joint;
struct Rigidbody;

/**
 * @brief Two colliders touching on a live tick, as the contact events track them across ticks.
 *
 * Not the solver's ContactCache: a trigger pair is never cached, and a contact ended by a destroyed
 * entity must name it, generation included, where the cache's id names the slot's next owner.
 */
struct TrackedPair {
    EntityId  a;                                       ///< The lower slot
    EntityId  b;
    bool      aTrigger = false;                        ///< a's collider is a trigger
    bool      bTrigger = false;                        ///< b's collider is a trigger
    glm::vec3 point    = glm::vec3(0.0f);              ///< The event's representative contact
    glm::vec3 normal   = glm::vec3(0.0f, 1.0f, 0.0f);  ///< That contact's normal, a -> b
    float     speed    = 0.0f;                         ///< How fast a and b closed along it, before the solve

    /**
     * @brief The order tracked pairs are kept and reported in: slot pair, generation, trigger flags.
     *
     * A function of the world alone, so every machine reports the same order. Pairs neither of which
     * comes first are the same contact.
     *
     * @param x The pair that may come first.
     * @param y The pair compared with.
     * @return True when @p x comes before @p y.
     */
    static bool before(const TrackedPair& x, const TrackedPair& y) {
        if (x.a.slot() != y.a.slot())             return x.a.slot() < y.a.slot();
        if (x.b.slot() != y.b.slot())             return x.b.slot() < y.b.slot();
        if (x.a.generation() != y.a.generation()) return x.a.generation() < y.a.generation();
        if (x.b.generation() != y.b.generation()) return x.b.generation() < y.b.generation();
        if (x.aTrigger != y.aTrigger)             return y.aTrigger;
        return !x.bTrigger && y.bTrigger;
    }
};

/**
 * @brief Fixed-step rigid-body dynamics: hands the scene's bodies and joints to vkmPhysics' step, and
 *        writes back what it answers.
 *
 * Runs on the tick in SystemStage::Simulation, ahead of HierarchySystem, so the Transforms it writes
 * reach WorldTransform the same frame. Enqueues CollisionEvent / TriggerEvent as contacts begin, last
 * and end, on live ticks only; a replayed tick already reported.
 *
 * A parented body simulates in world space (worldPoseOf) and is mapped back relative to its parent. The
 * parent's pose is taken once at gather, so a parent moving mid-tick is not followed, and a scaled
 * parent is not handled (a ColliderPart is unscaled).
 */
class PhysicsSystem : public System {
    public:
        PhysicsSystem() = default;
        ~PhysicsSystem() override = default;

        PhysicsSystem(const PhysicsSystem& other) = delete;
        PhysicsSystem& operator=(const PhysicsSystem& other) = delete;

        PhysicsSystem(PhysicsSystem && other) = delete;
        PhysicsSystem& operator=(PhysicsSystem && other) = delete;

    public:
        void fixedUpdate(FrameContext& ctx) override;

        /**
         * @brief Re-run during a replay: it moves the world from state and command, deterministically.
         *
         * @return Always true.
         */
        bool isReplayed() const override { return true; }

    private:
        /// What the step's contacts say of one body, read into its Rigidbody's support and block normals.
        struct Support {
            glm::vec3 normal  = {0.0f, -2.0f, 0.0f};  ///< Most upward; starts below any unit normal, so
                                                      ///< the first contact replaces it, even a wall's
            glm::vec3 block   = {0.0f, 1.0f, 0.0f};   ///< Most horizontal; starts at world up, which has
                                                      ///< no horizontal part
            uint32_t  body    = Physics::NO_BODY;     ///< The body whose surface gave normal, by index;
                                                      ///< set by the first normal that beats the start
            bool      touched = false;                ///< A manifold names it
        };

    private:
        /**
         * @brief Hand the scene's simulated bodies to the step: each Rigidbody as a Physics::Body, its
         *        collider's parts, in the storage's order, its id its entity's slot.
         *
         * A ragdoll bone the animation still drives is handed Kinematic and colliding with nothing: a
         * character's legs would shove any crate with infinite mass. A query still finds it, as a
         * hitbox. A parented body is handed its world pose (worldPoseOf).
         *
         * @param scene     Scene whose rigidbody entities are gathered.
         * @param resources Where a mesh collider's mesh resolves; its triangles are synced here.
         * @param net       Who decides each body; one this end does not decide is not simulated here.
         * @return False when there are no bodies, so the step is skipped.
         */
        bool gatherBodies(Scene& scene, const ResourceManager& resources, const NetSession& net);

        /**
         * @brief Hand the step each Joint component that joins two gathered bodies, or a body to an
         *        entity with a pose and no body (its world pose, fixed).
         *
         * Dropped: a joint whose entity is not simulated, that names itself, or names an entity with
         * neither body nor pose. Every joint's impulses are cleared here, so one dropped starts again
         * from nothing.
         *
         * @param scene Scene whose Joint components are gathered.
         */
        void gatherJoints(Scene& scene);

        /**
         * @brief The solver passes @p physics asks, held to PhysicsSettings' range.
         *
         * A scene file can say any number; one out of the range is said once a gap.
         *
         * @param physics The scene's settings.
         * @return Passes for the step.
         */
        int solverPasses(const PhysicsSettings& physics);

        /**
         * @brief Write the step's answer back: each body's velocities, sleep and contacts onto its
         *        Rigidbody, a moved one's pose onto its Transform through its parent's frame, and each
         *        joint's state onto its Joint.
         *
         * @param scene     Scene whose components are written.
         * @param replaying True during a replay, where undecided bodies get no support published.
         */
        void writeback(Scene& scene, bool replaying);

        /**
         * @brief Report how this live tick's touching pairs differ from the last live tick's, and keep them.
         *
         * In both: stayed; only now: began; only before: ended, whatever the cause. A pair whose bodies
         * both rest sends no Stayed, and wakes without beginning again. In TrackedPair::before order.
         * Never called in a replay.
         *
         * @param scene   The world, for whether a pair's bodies rest.
         * @param events  Bus the collision / trigger events are enqueued on.
         * @param touches The step's touching pairs; none when there was no step.
         */
        void reportContacts(const Scene& scene, EventBus& events, const std::vector<Physics::Touch>& touches);

        /**
         * @brief Tell the session which bodies this end's own character is on.
         *
         * A client predicts only what it owns, so a push would predict a wall. This hands the session the
         * island of pushable bodies, through this tick's contacts and joints, the owned body is in. Only on
         * a client with its own character. The island never crosses an immovable body or another
         * character, and holds only the server's bodies: one the client runs already would be run
         * again by every replay.
         *
         * @param scene The world, for turning body indices back into entities.
         * @param net   The session to report the island to.
         */
        void leaseContacts(Scene& scene, NetSession& net);

        /**
         * @brief Draw this tick's contacts while the host shows DebugCategory::Contacts: a dot at each
         *        point, and an arrow along its normal that grows with how hard the solver pushed there.
         *
         * The arrow points into a body the contact pushes, and is bounded however hard the push, so a
         * heavy body's stays readable.
         *
         * @param ctx Frame context supplying the host's DebugDraw and the tick's length.
         */
        void drawContacts(FrameContext& ctx) const;

    private:
        Physics::Stepper m_stepper;         ///< Steps the bodies; keeps what each pair carries
        uint64_t         m_worldEpoch = 0;  ///< Scene::epoch() the stepper's memory was made in

        // This tick's, cleared not freed. A body's id is its entity's slot.
        std::vector<Physics::Body>  m_bodies;           ///< The scene's bodies, as the step reads them
        std::vector<Physics::Part>  m_parts;            ///< Their colliders' parts, end to end
        std::vector<Physics::Joint> m_joints;           ///< The Joint components the step solves
        std::vector<EntityId>       m_entities;         ///< Each body's entity, by body index
        std::vector<Rigidbody*>     m_rigidbodies;      ///< Each body's Rigidbody, valid for the tick, which
                                                        ///< adds and removes none
        std::vector<glm::mat4>      m_parents;          ///< Each body's parent's world matrix, as gathered
        std::vector<Joint*>         m_jointOf;          ///< Each joint's component, by joint index
        std::vector<uint32_t>       m_bodyIndexBySlot;  ///< Entity slot -> body index, or NO_BODY. A vector:
                                                        ///< the key is already a dense small integer (see
                                                        ///< SlotAllocator).
        std::vector<bool>           m_posedBone;        ///< By slot: markPosedByAnimation's answer
        std::vector<EntityId>       m_leased;           ///< What leaseContacts hands the session
        std::vector<uint8_t>        m_leaseCarries;     ///< Per body: a push passes through it
        Physics::BodyIslands        m_leaseIslands;     ///< What those join into

        std::vector<Support> m_support;  ///< By body index, filled by writeback

        std::vector<TrackedPair> m_touching;          ///< The pairs touching on this live tick
        std::vector<TrackedPair> m_touchingLastTick;  ///< The pairs touching when the last live tick ended,
                                                      ///< in TrackedPair::before order. Event state, not
                                                      ///< simulation state: a replay leaves it alone, a
                                                      ///< replaced world drops it unreported.

        FaultLatch m_massless;       ///< A Dynamic body with no positive mass
        FaultLatch m_passesOutside;  ///< The scene's solverIterations outside their range
};

} // namespace Vkm::Engine
