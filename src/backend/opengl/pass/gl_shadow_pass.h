#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "system/render/render_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Renders shadow maps for every directional/spot/point light that has
 *        castShadows enabled, into a shared GLShadowAtlas.
 *
 * Each frame:
 *  1. Scans RenderView::lights for casters, allocating 2D layers (dir + spot)
 *     and cube layers (point) up to the per-type budgets in GLConfig::Limits.
 *  2. Builds the per-caster light-space matrix (or six, for cubes).
 *  3. Writes a ShadowCasterGPUData entry into the shadow UBO so the PBR
 *     shader can match a light to its map at lookup time.
 *  4. Renders shadow-castable opaque batches once per 2D layer and once per
 *     cube face. The batcher reuses the global instance buffer; the prefix
 *     `shadowInstanceCount` of each batch covers only castShadows=true drawables.
 *
 * Two shaders are used: a standard 2D depth shader for directional/spot
 * (writes projected depth) and a cube shader that writes linear distance /
 * range so a samplerCubeArrayShadow lookup works regardless of cube face.
 */
class GLShadowPass : public RenderPass {
    public:
        GLShadowPass() = delete;
        ~GLShadowPass() override = default;

        GLShadowPass(const GLShadowPass&) = delete;
        GLShadowPass& operator=(const GLShadowPass&) = delete;
        GLShadowPass(GLShadowPass&&) = delete;
        GLShadowPass& operator=(GLShadowPass&&) = delete;

        GLShadowPass(Core::Shader& shader2D, Core::Shader& shaderCube);

    public:
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

        /// Half-extent of the directional ortho frustum, world units. Sun shadows.
        void  setDirectionalOrthoRadius(float radius) { m_dirOrthoRadius = radius; }
        float getDirectionalOrthoRadius() const       { return m_dirOrthoRadius; }

        /// Distance the virtual directional shadow camera sits back along -lightDir.
        void  setDirectionalDistance(float dist)      { m_dirDistance = dist; }
        float getDirectionalDistance() const          { return m_dirDistance; }

        /// Slope-scaled bias maximum used by all 2D casters.
        void  setBias2D(float bias)                   { m_bias2D = bias; }
        float getBias2D() const                       { return m_bias2D; }

        /// Constant bias (in normalised distance) used by point shadows.
        void  setBiasCube(float bias)                 { m_biasCube = bias; }
        float getBiasCube() const                     { return m_biasCube; }

    private:
        struct PointJob {
            int       lightIndex   = -1;
            uint32_t  cubeIndex    = 0;
            glm::vec3 position{0.0f};
            float     range        = 1.0f;
            glm::mat4 faceMatrices[6]{};
        };

        struct Job2D {
            int       lightIndex = -1;
            uint32_t  layer      = 0;
            glm::mat4 lightSpace{1.0f};
        };

        void collectCasters(
            const RenderView& view,
            std::vector<Job2D>& jobs2D,
            std::vector<PointJob>& jobsCube
        );

        Core::Shader& m_shader2D;
        Core::Shader& m_shaderCube;

        float m_dirOrthoRadius = 80.0f;
        float m_dirDistance    = 120.0f;
        float m_dirNear        = 0.5f;
        float m_dirFar         = 300.0f;

        float m_bias2D    = 0.005f;
        float m_biasCube  = 0.005f;

        // Per-frame scratch reused to avoid heap churn.
        std::vector<Job2D>    m_jobs2D;
        std::vector<PointJob> m_jobsCube;
};

} // namespace Engine
