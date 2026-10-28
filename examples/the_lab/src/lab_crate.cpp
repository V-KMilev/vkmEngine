#include "lab_crate.h"

#include "ecs/component/core/transform.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"

#include "lab_state.h"
#include "lab_walker.h"

namespace Lab {

namespace {

// The least a strike must carry to knock a character down, kg m/s: a crate thrown at one carries
// well over it, and one at rest or rolling slowly is only something to walk into.
constexpr float KNOCK_DOWN_MOMENTUM = 10.0f;

} // namespace

void LabCrate::onStart() {
    if (isAuthority()) getOrAdd<LabTally>();
}

void LabCrate::onFixedUpdate(float dt) {
    if (!isAuthority()) return;
    if (const Rigidbody* body = tryGet<Rigidbody>()) m_momentum = body->linearVelocity * body->mass;
    m_age += dt;
    if (m_age >= lifetime) destroy();
}

void LabCrate::onCollisionEnter(const Collision& hit) {
    if (!isAuthority()) return;
    LabWalker* walker = findBehavior<LabWalker>(hit.other);
    if (walker && glm::length(m_momentum) >= KNOCK_DOWN_MOMENTUM) walker->knockDown(m_momentum, hit.point);
    LabTally* tally = tryGet<LabTally>();
    if (tally && tally->count == 0) ++tally->count;
}

void LabCrate::onUpdate(float) {
    const LabTally* tally = tryGet<LabTally>();
    if (!tally || !m_strikes.rose(tally->count)) return;
    const Transform* at = tryGet<Transform>();
    if (!at || impact.empty() || !window()) return;
    // Upright, from the crate's middle, whichever way it lies.
    Transform puff;
    puff.position = at->position;
    instantiate(impact, puff);
}

} // namespace Lab
