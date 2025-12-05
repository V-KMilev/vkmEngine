#include "mesh.h"

#include "gpu_mesh.h"
#include "gpu_material.h"

namespace Engine {

Mesh::Mesh(
    uint32_t id,
    uint32_t entityId
) : Component(id, ComponentType::Mesh),
    m_mesh(nullptr),
    m_material(nullptr) {}

void Mesh::setMesh(std::shared_ptr<GPUMesh> && mesh) {
    m_mesh = std::move(mesh);
}

void Mesh::setMaterial(std::shared_ptr<GPUMaterial> && material) {
    m_material = std::move(material);
}

std::shared_ptr<GPUMesh>& Mesh::getMesh() { return m_mesh; }
const std::shared_ptr<GPUMesh>& Mesh::getMesh() const { return m_mesh; }

std::shared_ptr<GPUMaterial>& Mesh::getMaterial() { return m_material; }
const std::shared_ptr<GPUMaterial>& Mesh::getMaterial() const { return m_material; }
} // namespace Engine
