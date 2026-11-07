#pragma once

#include <glm/glm.hpp>

#include "ecs/component/light/light.h"
#include "system/render/render_limits.h"

namespace Vkm::Engine {

inline constexpr float MAX_RECEIVER_SLOPE = 6.0f;  ///< The steepest receiver an orthographic tile's filter
                                                   ///< follows, as a tangent; past it the plane is the
                                                   ///< light's grazing edge, whose depth runs away.
                                                   ///< Shadow2DGPU::shape.w in the tile's own terms.

/**
 * @brief One 2D shadow caster (std140) - a held layer, a movers' tile or a spot map.
 *
 *   lightVP : world -> light clip space
 *   atlas   : xy = tile UV offset, zw = tile UV scale (sample = offset + uv*scale)
 *   params  : x = Light::SHADOW_BIAS, in texels at the receiver (biasSlide in
 *             shaders/opengl/include/shadows.glsl), y = world size of a shadow texel
 *             (a spot's at its range), z = far plane (the world depth an ortho tile's 0..1 spans;
 *             a spot's range), w = 1 for a movers' tile whose light has a size while soft
 *             shadows are on, which takes the filter over one compare; 0 otherwise
 *   shape   : x = normal-offset bias in this tile's texels (Light::SHADOW_NORMAL_BIAS),
 *             y = near plane of a perspective tile (spot), 0 if orthographic (cascade),
 *             z = tan of a spot's half field of view,
 *             w = the slope an orthographic tile's receiver plane takes at most, in depth per uv
 */
struct alignas(16) Shadow2DGPU {
    glm::mat4 lightVP = glm::mat4(1.0f);
    glm::vec4 atlas   = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    glm::vec4 params  = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    glm::vec4 shape   = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    /**
     * @brief An orthographic tile: a cascade's held layer or movers' tile, or a capture's one map.
     *
     * @param lightVP World -> the tile's clip space.
     * @param texel   World size of one of its texels.
     * @param width   World width it spans.
     * @param depth   World depth its 0..1 spans.
     * @param filter  params.w: 1 for the filter over one compare.
     * @return The tile, its atlas the whole map until its caller places it.
     */
    static Shadow2DGPU orthographic(
        const glm::mat4& lightVP,
        float texel,
        float width,
        float depth,
        float filter
    ) {
        Shadow2DGPU tile;
        tile.lightVP = lightVP;
        tile.params  = glm::vec4(Light::SHADOW_BIAS, texel, depth, filter);
        tile.shape   = glm::vec4(Light::SHADOW_NORMAL_BIAS, 0.0f, 0.0f, MAX_RECEIVER_SLOPE * width / depth);
        return tile;
    }
};

/**
 * @brief One cube shadow caster (std140) - a point light.
 *
 *   posRange : xyz = light world position, w = range, the faces' far plane
 *   params   : x = Light::SHADOW_BIAS and z = Light::SHADOW_NORMAL_BIAS, both in the cube's
 *              texels at the receiver; y = the faces' near plane
 */
struct alignas(16) ShadowCubeGPU {
    glm::vec4 posRange = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 params   = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
};

/**
 * @brief ShadowBlock UBO (std140) - must match ShadowBlock in shaders/opengl/include/shadows.glsl.
 *
 * cascadeSplits pick a cascade by distance from the eye. csmBase/csmCount mark the run in s2d that
 * holds what moves under the sun, a tile a cascade, read for the first moverCount of them; `held` is
 * what is fixed under it, read for the first heldCount. Spots take the rest of s2d, point lights
 * scube. A light carries its slot in the lights SSBO (GpuLight.spot.w).
 */
struct alignas(16) ShadowUBOData {
    glm::vec4     cascadeSplits = glm::vec4(0.0f);  ///< How far from the eye each cascade reaches.
    int           csmBase       = -1;
    int           csmCount      = 0;
    int           heldCount     = 0;
    int           moverCount    = 0;
    Shadow2DGPU   held [Config::NUM_CASCADES]{};
    Shadow2DGPU   s2d [Config::MAX_SHADOW_CASTERS_2D]{};
    ShadowCubeGPU scube[Config::MAX_SHADOW_CASTERS_CUBE]{};
};

} // namespace Vkm::Engine
