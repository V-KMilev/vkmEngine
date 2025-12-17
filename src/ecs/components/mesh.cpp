#include "mesh.h"

namespace Engine {

Mesh::Mesh(
    uint32_t id,
    MeshHandle mesh,
    MaterialHandle material,
    bool visible
) : Component(id, ComponentType::Mesh),
    m_mesh(mesh),
    m_material(material),
    m_visible(visible) {}


} // namespace Engine
