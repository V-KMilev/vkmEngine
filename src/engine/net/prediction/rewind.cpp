#include "net/prediction/rewind.h"

namespace Vkm::Engine {

void NetRewind::record(EntityId entity, uint32_t tick, const glm::vec3& position, const glm::quat& rotation) {
    m_tracks[entity.slot()].record(tick, position, rotation);
}

bool NetRewind::poseAt(EntityId entity, double tick, glm::vec3& position, glm::quat& rotation) const {
    const auto found = m_tracks.find(entity.slot());
    return found != m_tracks.end() && found->second.poseAt(tick, position, rotation);
}

void NetRewind::forget(EntityId entity) {
    m_tracks.erase(entity.slot());
}

} // namespace Vkm::Engine
