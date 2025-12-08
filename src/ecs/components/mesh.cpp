#include "mesh.h"

#include "gpu_mesh.h"

namespace Engine {

Mesh::Mesh(
    uint32_t id
) : Component(id, ComponentType::Mesh),
    m_mesh(nullptr),
    m_material(nullptr) {}

void Mesh::setMesh(std::shared_ptr<CPUMesh> && mesh) {
    m_mesh = std::move(mesh);
}

void Mesh::setMaterial(std::shared_ptr<CPUMaterial> && material) {
    m_material = std::move(material);
}

} // namespace Engine
