#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gl_shader.h"

#include "frame/gl_instance_batcher.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class GLObjectBuffer;
class GLView;
struct RenderObjects;

/**
 * @brief An offline shadow map's casters, drawn into its depth in their bind pose.
 *
 * Drawn with copies of the shadow pass's shaders: a caster whose material cuts its shadow through the
 * masked one (shadow/depth_masked), the rest through the plain one (shadow/depth). Not for the
 * frame's shadows, whose casters are posed and culled (GLShadowPass).
 */
class GLCasterDepth {
    public:
        GLCasterDepth();
        ~GLCasterDepth();

        GLCasterDepth(const GLCasterDepth& other) = delete;
        GLCasterDepth& operator=(const GLCasterDepth& other) = delete;

        GLCasterDepth(GLCasterDepth && other) = delete;
        GLCasterDepth& operator=(GLCasterDepth && other) = delete;

    public:
        /**
         * @brief Clear the bound target's @p side square of depth and draw @p casters into it.
         *
         * @param gl       The context.
         * @param casters  Objects of @p scene, by index.
         * @param scene    The objects.
         * @param glView   Their meshes and materials.
         * @param objects  Their per-object data, as uploaded.
         * @param lightVP  The map's view-projection, through GLDepth::shadowProjection.
         * @param side     Texels across the target.
         * @param meshes   Which of each caster's meshes is drawn (GLInstanceBatcher::buildGrouped).
         */
        void draw(
            Vkm::GL::Context& gl,
            const std::vector<uint32_t>& casters,
            const RenderObjects& scene,
            const GLView& glView,
            const GLObjectBuffer& objects,
            const glm::mat4& lightVP,
            uint32_t side,
            MeshOf meshes = MeshOf::Level
        );

    private:
        Vkm::GL::Shader   m_depth;    ///< shaders/shadow/depth
        Vkm::GL::Shader   m_masked;   ///< shaders/shadow/depth_masked
        GLInstanceBatcher m_batcher;  ///< The casters, grouped.
};

} // namespace Vkm::Engine
