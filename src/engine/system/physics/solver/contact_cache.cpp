#include "system/physics/solver/contact_cache.h"

#include <algorithm>

namespace Vkm::Engine {

namespace {

/**
 * @brief How far a contact point may have moved and still be the same contact.
 *
 * A resting contact moves a fraction of a millimetre between ticks, and a body
 * still settling moves what its speed allows - so the radius is loose enough to
 * follow one and tight enough that two points on one box face, which are the
 * width of the face apart, never trade impulses. A body moving faster than this
 * per tick simply gets no seed, which is what it had before there was one.
 */
constexpr float MATCH_RADIUS_SQ = 0.01f * 0.01f;

/// The pair a manifold belongs to: A's slot above B's, so the order is the manifold's.
uint64_t pairKey(EntityId a, EntityId b) {
    return (static_cast<uint64_t>(a.slot()) << 32) | static_cast<uint64_t>(b.slot());
}

} // namespace

void ContactCache::seed(std::vector<ContactManifold>& manifolds,
                        const std::vector<EntityId>& entities,
                        const std::vector<PhysicsBody>& bodies) const {
    if (m_held.empty()) return;

    for (ContactManifold& manifold : manifolds) {
        if (manifold.bodyA >= entities.size() || manifold.bodyB >= entities.size()) continue;

        const uint64_t key = pairKey(entities[manifold.bodyA], entities[manifold.bodyB]);
        const auto first = std::lower_bound(m_held.begin(), m_held.end(), key,
            [](const Held& held, uint64_t k) { return held.pair < k; });

        const glm::vec3 origin = bodies[manifold.bodyA].position;
        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            const glm::vec3 offset = contact.point - origin;

            // The nearest point this pair held, if it held one near enough.
            // Every shape pair reports its contacts in its own order, and an
            // order is not an identity: matching on it hands a corner the
            // impulse that belonged to the corner across the face.
            float best = MATCH_RADIUS_SQ;
            for (auto it = first; it != m_held.end() && it->pair == key; ++it) {
                const glm::vec3 delta = it->offset - offset;
                const float distanceSq = glm::dot(delta, delta);
                if (distanceSq >= best) continue;

                best = distanceSq;
                contact.normalImpulse = it->normalImpulse;

                // The friction impulse is kept as a vector and decomposed onto
                // this tick's basis, so a contact whose normal has turned since
                // keeps the force it was carrying rather than a pair of numbers
                // that meant something on an axis it no longer has.
                const glm::vec3 held = it->friction
                                     - glm::dot(it->friction, contact.normal) * contact.normal;
                contact.tangentImpulse1 = glm::dot(held, contact.tangent1);
                contact.tangentImpulse2 = glm::dot(held, contact.tangent2);
            }
        }
    }
}

void ContactCache::record(const std::vector<ContactManifold>& manifolds,
                          const std::vector<EntityId>& entities,
                          const std::vector<PhysicsBody>& bodies) {
    m_held.clear();

    for (const ContactManifold& manifold : manifolds) {
        if (manifold.bodyA >= entities.size() || manifold.bodyB >= entities.size()) continue;

        const uint64_t  key    = pairKey(entities[manifold.bodyA], entities[manifold.bodyB]);
        const glm::vec3 origin = bodies[manifold.bodyA].position;
        for (int c = 0; c < manifold.count; ++c) {
            const Contact& contact = manifold.contacts[c];
            const glm::vec3 friction = contact.tangentImpulse1 * contact.tangent1
                                     + contact.tangentImpulse2 * contact.tangent2;
            m_held.push_back(Held{key, contact.point - origin, friction, contact.normalImpulse});
        }
    }

    // Sorted so a pair is a range rather than a scan, and stably, so two
    // manifolds of one pair keep the order the narrowphase produced them in.
    std::stable_sort(m_held.begin(), m_held.end(),
                     [](const Held& a, const Held& b) { return a.pair < b.pair; });
}

void ContactCache::clear() {
    m_held.clear();
}

} // namespace Vkm::Engine
