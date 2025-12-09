#include "mesh.h"

namespace Engine {

Mesh::Mesh(
    uint32_t id,
    MeshHandle mesh,
    MaterialHandle material,
    bool visible,
    bool castsShadow
) : Component(id, ComponentType::Mesh),
    m_mesh(mesh),
    m_material(material),
    m_visible(visible),
    m_castsShadow(castsShadow) {}

void Mesh::setVisible(bool visible) { m_visible = visible; }
void Mesh::setCastsShadow(bool castsShadow) { m_castsShadow = castsShadow; }

} // namespace Engine
