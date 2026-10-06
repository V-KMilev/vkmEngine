#include "system/physics/solver/contact_cache.h"

#include <algorithm>

namespace Vkm::Engine {

namespace {

/**
 * @brief How far a contact point may have moved and still be the same contact.
 *
 * Loose enough to follow a settling body, tight enough that two points of one box face never trade
 * impulses. A point moving farther per tick on both bodies (a fast-rolling ball) gets no seed.
 */
constexpr float MATCH_RADIUS_SQ = 0.01f * 0.01f;

/// The pair a manifold belongs to: A's slot above B's, so the order is the manifold's.
uint64_t pairKey(EntityId a, EntityId b) {
    return (static_cast<uint64_t>(a.slot()) << 32) | static_cast<uint64_t>(b.slot());
}

} // namespace

void ContactCache::seed(
    std::vector<ContactManifold>& manifolds,
    const std::vector<EntityId>& entities,
    const std::vector<BodyFrame>& frames
) const {
    if (m_held.empty()) return;

    for (ContactManifold& manifold : manifolds) {
        if (manifold.bodyA >= entities.size() || manifold.bodyB >= entities.size()) continue;

        const uint64_t key = pairKey(entities[manifold.bodyA], entities[manifold.bodyB]);
        const auto first = std::lower_bound(
            m_held.begin(),
            m_held.end(),
            key,
            [](const Held& held, uint64_t k) { return held.pair < k; }
        );

        const BodyPose& a = frames[manifold.bodyA].pose;
        const BodyPose& b = frames[manifold.bodyB].pose;
        const glm::quat toA = glm::conjugate(a.rotation);
        const glm::quat toB = glm::conjugate(b.rotation);
        for (int c = 0; c < manifold.count; ++c) {
            Contact& contact = manifold.contacts[c];
            const glm::vec3 offsetA = toA * (contact.point - a.position);
            const glm::vec3 offsetB = toB * (contact.point - b.position);

            // Nearest on either body: a sliding box's corner stays put on the box, a riding one on the floor.
            // Not by report order, which can hand a corner the impulse of the one across the face.
            float best = MATCH_RADIUS_SQ;
            for (auto it = first; it != m_held.end() && it->pair == key; ++it) {
                const glm::vec3 deltaA = it->offsetA - offsetA;
                const glm::vec3 deltaB = it->offsetB - offsetB;
                const float distanceSq = std::min(glm::dot(deltaA, deltaA), glm::dot(deltaB, deltaB));
                if (distanceSq >= best) continue;

                best = distanceSq;
                contact.normalImpulse = it->normalImpulse;

                contact.warmFriction = it->friction - glm::dot(it->friction, contact.normal) * contact.normal;
            }
        }
    }
}

void ContactCache::record(
    const std::vector<ContactManifold>& manifolds,
    const std::vector<EntityId>& entities,
    const std::vector<BodyFrame>& frames
) {
    m_held.clear();

    for (const ContactManifold& manifold : manifolds) {
        if (manifold.bodyA >= entities.size() || manifold.bodyB >= entities.size()) continue;

        const uint64_t key = pairKey(entities[manifold.bodyA], entities[manifold.bodyB]);
        const glm::quat toA = glm::conjugate(frames[manifold.bodyA].pose.rotation);
        const glm::quat toB = glm::conjugate(frames[manifold.bodyB].pose.rotation);
        for (int c = 0; c < manifold.count; ++c) {
            const Contact& contact = manifold.contacts[c];
            const glm::vec3 friction = contact.tangentImpulse1 * contact.tangent1
                + contact.tangentImpulse2 * contact.tangent2;
            // Against the gathered pose, as seed measures; the integrated pose is off by the tick's travel,
            // the whole match radius at 0.6 m/s.
            m_held.push_back(Held{
                key,
                toA * contact.rA,
                toB * contact.rB,
                friction,
                contact.normalImpulse,
                static_cast<uint32_t>(m_held.size())
            });
        }
    }

    // A pair is a range; within it, record order, since seed keeps the first of two equally near points.
    // Keyed on both because stable_sort allocates on every call.
    std::sort(m_held.begin(), m_held.end(), [](const Held& a, const Held& b) {
        if (a.pair != b.pair) return a.pair < b.pair;
        return a.sequence < b.sequence;
    });
}

void ContactCache::clear() {
    m_held.clear();
}

} // namespace Vkm::Engine
