#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gl_pass.h"
#include "data/gl_instance_buffer.h"

#include "data/gl_shadow_data.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Renders all shadow depth maps for the frame, ahead of the forward pass.
 *
 * Consumes the plan in GLShadowData (built by the backend): directional cascades
 * and spot maps go into the 2D depth atlas, point lights into cube maps. A
 * single depth-only draw of every castShadows drawable feeds each tile / face.
 * A no-op when the frame has no shadow casters.
 */
class GLShadowPass : public GLPass {
    public:
        GLShadowPass();
        ~GLShadowPass() override;

        GLShadowPass(const GLShadowPass& other) = delete;
        GLShadowPass& operator=(const GLShadowPass& other) = delete;

        GLShadowPass(GLShadowPass && other) = delete;
        GLShadowPass& operator=(GLShadowPass && other) = delete;

    public:
        void execute(GLFrameContext& ctx) override;

    private:
        /**
         * @brief Fill the 2D depth atlas for directional cascades and spots.
         *
         * Clears the atlas once, then rasterises every shadow caster into each
         * cascade / spot tile with the projected-depth shader. A no-op when the
         * frame has no 2D casters.
         */
        void render2D(GLFrameContext& ctx);

        /**
         * @brief Fill each point light's depth cube.
         *
         * Six faces per light, rendered with the distance-depth shader so the
         * forward pass can sample the cube by direction.
         */
        void renderCube(GLFrameContext& ctx);

        /**
         * @brief Draw one tile's or face's pre-culled shadow casters.
         *
         * Submission only: the culling against the light's clip space and the
         * mesh sort both happened on the thread pool (GLShadowData::cullCasters),
         * so this flattens the batch's models into one upload and issues one
         * instanced depth draw per mesh run. The caller has already bound the
         * depth shader and set its matrices.
         *
         * @param ctx   The frame context, for the GL view and the caster list.
         * @param batch The tile's / face's surviving caster indices, mesh-sorted.
         */
        void renderCasters(GLFrameContext& ctx, const ShadowCasterBatch& batch);

    private:
        std::unique_ptr<Core::Shader> m_depth2D;    ///< Projected depth (cascades + spots).
        std::unique_ptr<Core::Shader> m_depthCube;  ///< Linear distance depth (point faces).

        Core::InstanceBuffer   m_instances;  ///< Per-caster model matrices (loc 4-7).
        std::vector<glm::mat4> m_models;     ///< Flattened models of every surviving caster this tile/face.
};

} // namespace Engine
