#pragma once

#include <memory>

#include <glm/glm.hpp>

#include "render_pass.h"
#include "gl_mesh.h"

namespace Core {
    class Shader;
}

namespace Engine {

class SpatialIndex;

/**
 * @brief Render pass for visualizing BVH (Bounding Volume Hierarchy) structure.
 *
 * Draws wireframe boxes for BVH nodes at configurable depth levels.
 * Useful for debugging spatial partitioning and understanding BVH structure.
 */
class GLBVHDebugPass : public RenderPass {
public:
    GLBVHDebugPass() = delete;
    ~GLBVHDebugPass() = default;

    GLBVHDebugPass(const GLBVHDebugPass& other) = delete;
    GLBVHDebugPass& operator=(const GLBVHDebugPass& other) = delete;

    GLBVHDebugPass(GLBVHDebugPass&& other) = delete;
    GLBVHDebugPass& operator=(GLBVHDebugPass&& other) = delete;

    /**
     * @brief Construct a GLBVHDebugPass.
     * @param shader Reference to shader for rendering wireframes.
     * @param spatialIndex Reference to the spatial index containing the BVH.
     */
    GLBVHDebugPass(Core::Shader& shader, const SpatialIndex& spatialIndex);

    void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
    void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

    /**
     * @brief Set the maximum depth of BVH nodes to visualize.
     * @param depth Max depth (0 = root only, -1 = all levels).
     */
    void setMaxDepth(int depth) { m_maxDepth = depth; }
    int getMaxDepth() const { return m_maxDepth; }

    /**
     * @brief Set whether to only show leaf nodes.
     */
    void setShowLeavesOnly(bool leavesOnly) { m_showLeavesOnly = leavesOnly; }
    bool getShowLeavesOnly() const { return m_showLeavesOnly; }

    /**
     * @brief Enable/disable the debug pass.
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

private:
    void initialize();

    Core::Shader& m_shader;
    const SpatialIndex& m_spatialIndex;

    std::unique_ptr<GLMesh> m_wireframeCube;

    int m_maxDepth = -1;        // -1 = show all levels
    bool m_showLeavesOnly = false;
    bool m_enabled = false;     // Disabled by default for performance
};

} // namespace Engine

