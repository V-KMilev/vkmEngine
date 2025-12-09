#include "gl_view.h"

#include <stdexcept>

#include "logger.h"

#include "render_view.h"
#include "resource_manager.h"

#include "gl_mesh.h"

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
            m_versions[key] = version;

        } else if (m_versions[key] != version) {
            // The mesh asset has changed since last sync: update the GLMesh on GPU
            it->second->update(asset);
            m_versions[key] = version;
        }
        // If mesh exists and version matches, nothing needs to be done
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

} // namespace Engine