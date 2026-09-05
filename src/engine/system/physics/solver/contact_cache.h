#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/physics/collision/contact.h"
#include "system/physics/solver/solver.h"

namespace Vkm::Engine {

/**
 * @brief What held each contacting pair when the last tick finished.
 *
 * Sequential impulses converge on their answer from wherever they are started,
 * and the answer barely changes between ticks: a box resting on a box needs the
 * same impulse this tick as last. Starting from zero instead spends the whole
 * iteration budget rediscovering it, and never quite arrives - which is what a
 * stack sinking into itself and leaning over is.
 *
 * Contacts are matched to the tick before by where they are rather than by a
 * feature index, because the narrowphase generates points rather than naming
 * the features that produced them, and a point is what every shape pair has in
 * common. Stored relative to body A, so a pair resting on a moving platform
 * matches itself as readily as one on the floor.
 *
 * A pair that stops touching stops being recorded, so the store is what is
 * touching now rather than everything that ever did.
 *
 * Not part of a rollback: a replayed tick is seeded from whatever the last
 * simulated tick left, not from what the tick being replayed had. The seed is a
 * starting guess that the passes then correct, so this costs a replay some
 * precision rather than its answer.
 */
class ContactCache {
    public:
        ContactCache() = default;
        ~ContactCache() = default;

        ContactCache(const ContactCache& other) = delete;
        ContactCache& operator=(const ContactCache& other) = delete;

        ContactCache(ContactCache && other) = delete;
        ContactCache& operator=(ContactCache && other) = delete;

    public:
        /**
         * @brief Give every contact the impulse its pair carried last tick.
         *
         * @param manifolds This tick's manifolds, whose contacts are seeded in place.
         * @param entities Body entities, indexed by the manifolds' body indices.
         * @param bodies This tick's solver bodies, for the frame a point is stored in.
         */
        void seed(std::vector<ContactManifold>& manifolds,
                  const std::vector<EntityId>& entities,
                  const std::vector<PhysicsBody>& bodies) const;

        /**
         * @brief Replace the store with what the solve just settled on.
         *
         * @param manifolds The solved manifolds, carrying their final impulses.
         * @param entities Body entities, indexed by the manifolds' body indices.
         * @param bodies This tick's solver bodies, for the frame a point is stored in.
         */
        void record(const std::vector<ContactManifold>& manifolds,
                    const std::vector<EntityId>& entities,
                    const std::vector<PhysicsBody>& bodies);

        /// Forget everything, for a world that is being replaced.
        void clear();

    private:
        /// One contact's share of the load, in body A's frame.
        struct Held {
            uint64_t  pair     = 0;                   ///< The two entity slots, A in the high half.
            glm::vec3 offset   = {0.0f, 0.0f, 0.0f};  ///< Contact point relative to body A.
            glm::vec3 friction = {0.0f, 0.0f, 0.0f};  ///< Friction impulse in world space.
            float normalImpulse = 0.0f;
        };

    private:
        std::vector<Held> m_held;   ///< Sorted by pair, so a pair is a binary search.
};

} // namespace Vkm::Engine
