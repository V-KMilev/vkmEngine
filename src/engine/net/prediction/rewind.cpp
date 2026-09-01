#include "net/prediction/rewind.h"

#include <algorithm>

#include "net/net_session.h"

namespace Vkm::Engine {

void NetRewind::record(EntityId entity, uint32_t tick,
                       const glm::vec3& position, const glm::quat& rotation) {
    m_tracks[entity.slot()].record(tick, position, rotation);
}

bool NetRewind::poseAt(EntityId entity, float tick,
                       glm::vec3& position, glm::quat& rotation) const {
    const auto found = m_tracks.find(entity.slot());
    return found != m_tracks.end() && found->second.poseAt(tick, position, rotation);
}

void NetRewind::forget(EntityId entity) {
    m_tracks.erase(entity.slot());
}

NetRewindScope::NetRewindScope(Scene& scene, NetSession& net, EntityId shooter)
    : m_scene(scene), m_net(net) {
    m_net.beginRewind(m_scene, shooter);
}

NetRewindScope::~NetRewindScope() {
    m_net.endRewind(m_scene);
}

} // namespace Vkm::Engine
