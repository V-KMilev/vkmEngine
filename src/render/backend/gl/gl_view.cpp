#include "gl_view.h"

#include <stdexcept>

#include "logger.h"

#include "render_view.h"
#include "resource_manager.h"

#include "gl_mesh.h"
#include "gl_material.h"

namespace Engine {

void GLView::syncMeshes(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    for (const auto& instance : renderView.instances) {
        // Skip non-visible instances
        if (!instance.visible) {
            continue;
        }
        // Skip instances which have no mesh handle set
        if (!instance.mesh) {
            continue;
        }

        // Fetch the mesh asset from the resource manager
        const uint32_t key     = instance.mesh.value;
        const auto& asset      = resourceManager.getMesh(instance.mesh);
        const uint64_t version = asset.version;

        // Try to find an existing GLMesh mapped to this handle
        auto it = m_meshes.find(key);

        if (it == m_meshes.end()) {
            // No GLMesh for this mesh: create it from the asset and track its version
            m_meshes[key] = std::make_unique<GLMesh>(asset);
            m_meshVersions[key] = version;

        } else if (m_meshVersions[key] != version) {
            // The mesh asset has changed since last sync: update the GLMesh on GPU
            it->second->update(asset);
            m_meshVersions[key] = version;
        }
        // If mesh exists and version matches, nothing needs to be done
    }
}

void GLView::syncMaterials(
    const RenderView& renderView,
    const ResourceManager& resourceManager
) {
    for (const auto& instance : renderView.instances) {
        // Skip non-visible instances
        if (!instance.visible) {
            continue;
        }
        // Skip instances which have no material handle set
        if (!instance.material) {
            continue;
        }

        // Fetch the material asset from the resource manager
        const uint32_t key     = instance.material.value;
        const auto& asset      = resourceManager.getMaterial(instance.material);
        const uint64_t version = asset.version;

        // Try to find an existing GLMaterial mapped to this handle
        auto it = m_materials.find(key);

        if (it == m_materials.end()) {
            // No GLMaterial for this material: create it from the asset and track its version
            m_materials[key] = std::make_unique<GLMaterial>(asset);
            m_materialVersions[key] = version;

        } else if (m_materialVersions[key] != version) {
            // The material asset has changed since last sync: update the GLMaterial
            it->second->update(asset);
            m_materialVersions[key] = version;
        }
        // If material exists and version matches, nothing needs to be done
    }
}

const GLMesh& GLView::getMesh(const MeshHandle& handle) const {
    auto it = m_meshes.find(handle.value);

    // TODO: Handle this case
    if (it == m_meshes.end() || !it->second) {
        LOG_ERROR("Mesh not synced");
    }

    return *it->second;
}

const GLMaterial& GLView::getMaterial(const MaterialHandle& handle) const {
    auto it = m_materials.find(handle.value);

    // TODO: Handle this case
    if (it == m_materials.end() || !it->second) {
        LOG_ERROR("Material not synced");
    }

    return *it->second;
}

} // namespace Engine