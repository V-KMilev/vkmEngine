#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "resource_handle.h"
#include "gl_mesh.h"

namespace Engine {
    class RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief GLView manages the OpenGL-side resources and synchronization for all
 * drawable mesh instances required for a render pass.
 * 
 * This class maintains an up-to-date mapping between engine-level mesh handles
 * and concrete OpenGL mesh objects (GLMesh). It ensures all required GPU geometry
 * is available and updated, coordinating with the resource manager and current scene data.
 */
class GLView {
    public:
        GLView() = default;
        ~GLView() = default;

        GLView(const GLView& other) = delete;
        GLView& operator=(const GLView& other) = delete;

        GLView(GLView && other) = delete;
        GLView& operator=(GLView && other) = delete;

    public:
        /**
        * @brief Synchronize OpenGL mesh objects with those referenced in the current RenderView.
        *
        * For each mesh handle in renderView, ensures a corresponding GLMesh exists and is up-to-date
        * with its latest version from the resource manager. Creates a new GLMesh or updates an
        * existing one as necessary.
        *
        * @param renderView The view description for the current render pass.
        * @param resourceManager The resource manager for accessing mesh assets.
        */
        void syncMeshes(const RenderView& renderView, const ResourceManager& resourceManager);

        /**
        * @brief Obtain the OpenGL mesh (GLMesh) for the given engine mesh handle.
        *
        * Throws or asserts if the mesh is not found (should be called after a successful syncMeshes).
        *
        * @param handle The mesh handle to look up.
        * @return The corresponding GLMesh object.
        */
        const GLMesh& getMesh(const MeshHandle& handle) const;

    private:
        std::unordered_map<uint32_t, std::unique_ptr<GLMesh>> m_meshes;
        std::unordered_map<uint32_t, uint64_t> m_versions;
};

} // namespace Engine