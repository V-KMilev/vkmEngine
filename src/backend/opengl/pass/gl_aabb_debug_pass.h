#pragma once

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "resource/shader_asset.h"
#include "system/render/render_pass.h"

#include "resource/gl_mesh.h"

#include "gl_instance_buffer.h"  // Core::InstanceBuffer (vkmGL)

namespace Core {
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;
}

namespace Engine {

/**
 * @brief Render pass for drawing AABB (Axis-Aligned Bounding Box) wireframes for debugging.
 * 
 * The GLAABBDebugPass draws wireframe boxes representing the bounding boxes of all
 * visible drawables in the scene. This is useful for debugging frustum culling and
 * visualizing the spatial bounds of objects.
 */
class GLAABBDebugPass : public RenderPass {
    public:
        GLAABBDebugPass() = delete;
        ~GLAABBDebugPass() = default;

        GLAABBDebugPass(const GLAABBDebugPass& other) = delete;
        GLAABBDebugPass& operator=(const GLAABBDebugPass& other) = delete;

        GLAABBDebugPass(GLAABBDebugPass && other) = delete;
        GLAABBDebugPass& operator=(GLAABBDebugPass && other) = delete;

        /**
         * @brief Construct a GLAABBDebugPass that draws AABBs with a given shader.
         * @param shader Reference to an OpenGL shader to be used for rendering AABBs.
         */
        explicit GLAABBDebugPass(ShaderHandle shader);

    public:
        /**
         * @brief Respond to a framebuffer or window resize.
         * 
         * Updates internal state or resources as necessary when the output framebuffer changes size.
         * @param backend Reference to the render backend.
         * @param width   New width in pixels.
         * @param height  New height in pixels.
         */
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;

        /**
         * @brief Execute the AABB debug pass for the current frame.
         * 
         * Draws wireframe boxes representing the AABBs of all visible drawables.
         * @param backend   Reference to the current render backend (should be OpenGL).
         * @param view      Provides scene and camera information.
         * @param resources Access to GPU mesh and material handles for this frame.
         */
        void execute(RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            builder.write(RGResource::SceneHDR);
        }

    private:
        /**
         * @brief Initialize the unit cube wireframe mesh if not already created.
         * 
         * Creates a unit cube (from -0.5 to +0.5) with wireframe edges.
         */
        void initialize();

    private:
        ShaderHandle m_shader;

        std::unique_ptr<GLMesh> m_aabb;
        Core::InstanceBuffer m_instanceBuffer;
        std::vector<glm::mat4> m_modelScratch;
};

} // namespace Engine

