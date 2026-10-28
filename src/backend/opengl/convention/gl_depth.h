#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "gl_texture.h"

namespace Vkm::Engine {

/**
 * @brief The backend's depth, once: clip z runs 0..w (glClipControl), the eye's reversed.
 *
 * The engine's projections are glm's, clip z in -w..w. Whatever draws for the eye goes through
 * eyeProjection: the near plane at depth 1, the far at 0, into a float depth buffer, which then
 * spends its precision where perspective took it away - distant surfaces do not fight. A shadow
 * map goes through shadowProjection and keeps near 0, far 1: most are orthographic, whose depth is
 * linear and gains nothing reversed. The shaders' side is shaders/depth.glsl.
 */
namespace GLDepth {
    inline constexpr float  EYE_NEAR        = 1.0f;         ///< The eye's near plane.
    inline constexpr float  EYE_CLEAR       = 0.0f;         ///< The eye's far plane.
    inline constexpr GLenum EYE_TEST        = GL_GREATER;   ///< Nearer is greater.
    inline constexpr GLenum EYE_TEST_EQUAL  = GL_GEQUAL;    ///< For a pass over the prepass's depth.
    inline constexpr GLenum EYE_FORMAT      = GL_DEPTH_COMPONENT32F;

    inline constexpr float  SHADOW_CLEAR    = 1.0f;         ///< A shadow map's far plane.
    inline constexpr GLenum SHADOW_TEST     = GL_LESS;
    /// How a sampler compares a shadow map's depth: lit where nearer, as SHADOW_TEST drew it.
    inline constexpr Vkm::GL::TextureCompare SHADOW_COMPARE = Vkm::GL::TextureCompare::LessEqual;

    /**
     * @brief @p projection with clip z remapped by @p zFromW and @p zFromZ, in double.
     *
     * The rows are near-equal where the remap matters, so their difference is taken before the
     * float rounding rather than after it.
     *
     * @param projection The matrix whose clip z row is remapped.
     * @param zFromW     Clip w's share of the new clip z.
     * @param zFromZ     Clip z's share of the new clip z.
     * @return The remapped matrix, rounded to float once.
     */
    inline glm::mat4 remapClipZ(const glm::mat4& projection, double zFromW, double zFromZ) {
        glm::dmat4 out(projection);
        for (int column = 0; column < 4; ++column) {
            out[column][2] = zFromW * out[column][3] + zFromZ * out[column][2];
        }
        return glm::mat4(out);
    }

    /**
     * @brief A projection (or any matrix ending in one) as the eye draws: depth 1 near, 0 far.
     *
     * @param projection glm's, clip z in -w..w.
     * @return The same, clip z = (w - z) / 2.
     */
    inline glm::mat4 eyeProjection(const glm::mat4& projection) {
        return remapClipZ(projection, 0.5, -0.5);
    }

    /**
     * @brief The eye's window depth of a point glm's projection put at clip @p z, @p w.
     *
     * @param z Clip z, in -w..w.
     * @param w Clip w.
     * @return eyeProjection's depth of it: 1 near, 0 far.
     */
    inline float eyeDepth(float z, float w) {
        return 0.5f - 0.5f * z / w;
    }

    /**
     * @brief A projection (or any matrix ending in one) as a shadow map draws: depth 0 near, 1 far.
     *
     * @param projection glm's, clip z in -w..w.
     * @return The same, clip z = (w + z) / 2.
     */
    inline glm::mat4 shadowProjection(const glm::mat4& projection) {
        return remapClipZ(projection, 0.5, 0.5);
    }
} // namespace GLDepth

} // namespace Vkm::Engine
