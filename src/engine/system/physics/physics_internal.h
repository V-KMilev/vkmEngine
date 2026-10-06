#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/bounds.h"
#include "ecs/entity.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "system/physics/body_pose.h"
#include "system/physics/collision/contact.h"

namespace Vkm::Engine {

/**
 * @brief Broadphase and narrowphase view of one collidable body, cached per tick.
 *
 * `body` indexes the parallel solver-body array. `immovable` is a zero BodyFrame::invMass plus the bodies a
 * replay is not deciding; a sleeping dynamic body is not, since its contacts support what rests on it.
 */
struct ColliderProxy {
    uint32_t body = 0;

    /**
     * @brief This body's parts already placed in world space, as two spans.
     *
     * Monomorphic arrays, not a tagged list, so the quadratic pair loops test shape once per part. Filled
     * once per body at gather. A mesh part is in neither: it is walked per pair against the other bound.
     */
    uint32_t boxFirst     = 0;
    uint32_t boxCount     = 0;
    uint32_t capsuleFirst = 0;
    uint32_t capsuleCount = 0;

    /**
     * @brief The component this proxy was built from: its parts, and a mesh part's triangles and tree.
     *
     * Read through, not copied; safe because nothing adds or removes a component between gather and the
     * narrowphase.
     */
    const Collider* collider = nullptr;
    Math::AABB bounds;  ///< World bound of every part together
    bool immovable = false;
    bool fixed     = false;  ///< RigidbodyMotion::Static: never moves at all, unlike a kinematic body.

    // Copied at gather, so the pair loop does not reach back into the scene.
    bool isTrigger = false;
    int layer = 1;
    int collidesWith = ~0;
};

/**
 * @brief Broadphase pairs in one chunk of the narrowphase, the unit handed to the pool.
 *
 * A tick with no more pairs than this runs on the calling thread. It decides who computes a pair, never
 * the result.
 */
inline constexpr uint32_t NARROWPHASE_CHUNK_PAIRS = 64;

/**
 * @brief A broadphase pair that touched this tick, as its chunk hands it to the merge.
 */
struct TouchingPair {
    uint32_t  pair          = 0;                            ///< Index into the tick's broadphase pairs
    /// Its manifolds, next in the chunk's list; none for a trigger
    uint32_t  manifoldCount = 0;
    /// First contact found, the event's representative
    glm::vec3 point         = glm::vec3(0.0f);
    glm::vec3 normal        = glm::vec3(0.0f, 1.0f, 0.0f);  ///< That contact's normal, A -> B
};

/**
 * @brief Two colliders touching on a live tick, as the contact events track them across ticks.
 *
 * Not the solver's ContactCache: a trigger pair is never cached, and a contact ended by a destroyed
 * entity must name it, generation included, where the cache's slot key names the slot's next owner.
 */
struct TrackedPair {
    EntityId  a;                                       ///< The lower slot
    EntityId  b;
    bool      aTrigger = false;                        ///< a's collider is a trigger
    bool      bTrigger = false;                        ///< b's collider is a trigger
    glm::vec3 point    = glm::vec3(0.0f);              ///< The event's representative contact
    glm::vec3 normal   = glm::vec3(0.0f, 1.0f, 0.0f);  ///< That contact's normal, a -> b

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
 * @brief Everything one narrowphase chunk writes, so chunks on different threads share nothing.
 *
 * Cleared, not rebuilt, so it stops allocating after its busiest tick. Merged in chunk then pair order,
 * so the result matches one thread sweeping every pair.
 */
struct NarrowphaseChunk {
    std::vector<TouchingPair>    touching;        ///< In pair order
    std::vector<ContactManifold> manifolds;       ///< The touching pairs', end to end, same order
    std::vector<uint32_t>        meshCandidates;  ///< Scratch: the triangles a tree walk found
};

/**
 * @brief Disjoint sets over the tick's bodies, for grouping what rests together.
 *
 * Rebuilt each tick into the same storage, so the tick allocates nothing.
 */
class BodyIslands {
    public:
        BodyIslands() = default;
        ~BodyIslands() = default;

        BodyIslands(const BodyIslands& other) = delete;
        BodyIslands& operator=(const BodyIslands& other) = delete;

        BodyIslands(BodyIslands && other) = delete;
        BodyIslands& operator=(BodyIslands && other) = delete;

    public:
        /// Every body its own island again.
        void reset(size_t count) {
            m_parent.resize(count);
            m_size.assign(count, 1);
            for (size_t i = 0; i < count; ++i) m_parent[i] = static_cast<uint32_t>(i);
        }

        uint32_t find(uint32_t i) {
            while (m_parent[i] != i) {
                m_parent[i] = m_parent[m_parent[i]];
                i = m_parent[i];
            }
            return i;
        }

        void join(uint32_t a, uint32_t b) {
            a = find(a);
            b = find(b);
            if (a == b) return;
            if (m_size[a] < m_size[b]) std::swap(a, b);
            m_parent[b] = a;
            m_size[a]  += m_size[b];
        }

    private:
        std::vector<uint32_t> m_parent;
        std::vector<uint32_t> m_size;
};

/**
 * @brief Per-tick body state cached at gather: mass properties, and the frame writeback maps through.
 *
 * The pose the solve moves is PhysicsBody's. Mass properties are re-derived from Rigidbody + Collider
 * every tick, so edits need no "apply".
 */
struct BodyFrame {
    /// Looked up at gather; valid for the tick, which adds and removes no Rigidbody.
    Rigidbody*      rb              = nullptr;
    BodyPose        pose;                        ///< As gathered, parent frame included.
    /// Rigidbody::motion, or Kinematic for a bone a ragdoll poses (isPosedByAnimation)
    RigidbodyMotion motion          = RigidbodyMotion::Dynamic;
    bool            decided         = false;     ///< This end decides where it goes; always true offline.
    float           invMass         = 0.0f;      ///< 1/mass this tick; 0 = nothing the solver does moves it.
    float           reach           = 0.0f;      ///< Farthest the collider extends from the origin, m.
    /// Body-local inverse inertia; 0 = no rotational response.
    glm::mat3       invInertiaLocal = glm::mat3(0.0f);
};

/**
 * @brief What one tick's contacts add up to for one body, in the two reductions Rigidbody publishes.
 *
 * Filled by the narrowphase, read after the solve. Both normals act on THIS body, so a contact's two
 * bodies see opposite directions.
 */
struct BodyContacts {
    // Below any unit normal, so the first contact replaces it, even a vertical wall's.
    glm::vec3 support = {0.0f, -2.0f, 0.0f};  ///< Most upward contact normal

    // World up has no horizontal part, so level floor leaves "nothing in the way".
    glm::vec3 block = {0.0f, 1.0f, 0.0f};  ///< Most horizontal contact normal

    bool touched = false;  ///< A resolved, non-trigger contact reached this body
};

} // namespace Vkm::Engine
