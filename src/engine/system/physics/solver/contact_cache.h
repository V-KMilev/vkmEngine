#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/physics/physics_internal.h"
#include "system/physics/collision/contact.h"

namespace Vkm::Engine {

/**
 * @brief What held each contacting pair when the last tick finished.
 *
 * The answer barely changes between ticks, so it seeds sequential impulses. Contacts match by position,
 * since the narrowphase names no features, kept in both bodies' frames and matched in either, so a pair
 * on a moving platform or a box sliding over the floor matches itself.
 *
 * Not rolled back: a replay is seeded from the last simulated tick. The seed is a guess the passes
 * correct, so this costs a replay precision, not its answer.
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
         * @param manifolds This tick's manifolds, seeded in place.
         * @param entities Body entities, indexed by the manifolds' body indices.
         * @param frames Each body's pose as gathered, the frame a point is kept in.
         */
        void seed(
            std::vector<ContactManifold>& manifolds,
            const std::vector<EntityId>& entities,
            const std::vector<BodyFrame>& frames
        ) const;

        /**
         * @brief Replace the store with what the solve just settled on.
         *
         * Stores the solve's lever arms in each body's frame as gathered, before the tick moved it, as
         * seed measures.
         *
         * @param manifolds The solved manifolds, with final impulses and lever arms.
         * @param entities Body entities, indexed by the manifolds' body indices.
         * @param frames Each body's pose as gathered, not as the tick left it.
         */
        void record(
            const std::vector<ContactManifold>& manifolds,
            const std::vector<EntityId>& entities,
            const std::vector<BodyFrame>& frames
        );

        /// Forget everything, for a world that is being replaced.
        void clear();

    private:
        /// One contact's share of the load, and where it was on each body.
        struct Held {
            uint64_t  pair          = 0;                   ///< The two entity slots, A in the high half.
            /// Contact point relative to body A, in A's frame.
            glm::vec3 offsetA       = {0.0f, 0.0f, 0.0f};
            /// Contact point relative to body B, in B's frame.
            glm::vec3 offsetB       = {0.0f, 0.0f, 0.0f};
            glm::vec3 friction      = {0.0f, 0.0f, 0.0f};  ///< Friction impulse in world space.
            float     normalImpulse = 0.0f;                ///< What the next tick's solve starts from.
            uint32_t  sequence      = 0;                   ///< Record order, for ties within a pair.
        };

    private:
        std::vector<Held> m_held;  ///< Sorted by pair, so a pair is a binary search.
};

} // namespace Vkm::Engine
