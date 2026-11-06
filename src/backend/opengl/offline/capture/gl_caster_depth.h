#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "vkm_opengl/shader/shader.h"

#include "frame/draw/gl_instance_batcher.h"

namespace Vkm::OpenGL {
    class Context;
}

namespace Vkm::Engine {

class GLObjectBuffer;
class GLAssetMirror;
struct RenderObjects;

/**
 * @brief An offline shadow map's casters, drawn into its depth in their bind pose.
 *
 * Drawn with copies of the shadow pass's shaders: a caster whose material cuts its shadow through the
 * masked one (scene/shadow/masked), the rest through the plain one (scene/shadow). Not for the
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
         * @brief Clear the bound target's @p side square of depth, unless @p clear is off, and draw
         *        @p casters into it.
         *
         * @param gl       The context.
         * @param casters  Objects of @p scene, by index.
         * @param scene    The objects.
         * @param mirror   Their meshes and materials.
         * @param objects  Their per-object data, as uploaded.
         * @param lightVP  The map's view-projection, through GLDepth::shadowProjection.
         * @param side     Texels across the target.
         * @param meshes   Which of each caster's meshes is drawn (GLInstanceBatcher::buildGrouped).
         * @param clear    Off to draw over what an earlier call drew, as a map drawn in parts is.
         */
        void draw(
            Vkm::OpenGL::Context& gl,
            const std::vector<uint32_t>& casters,
            const RenderObjects& scene,
            const GLAssetMirror& mirror,
            const GLObjectBuffer& objects,
            const glm::mat4& lightVP,
            uint32_t side,
            MeshOf meshes = MeshOf::Level,
            bool clear = true
        );

    private:
        Vkm::OpenGL::Shader m_depth;    ///< shaders/opengl/scene/shadow
        Vkm::OpenGL::Shader m_masked;   ///< shaders/opengl/scene/shadow/masked
        GLInstanceBatcher   m_batcher;  ///< The casters, grouped.
};

} // namespace Vkm::Engine
