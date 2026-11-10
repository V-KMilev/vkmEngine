#pragma once

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "base/math/aabb.h"

namespace Vkm::Engine {

/**
 * @brief One billboard particle for the frame, flattened from the emitters.
 *
 * Six vec4s so it uploads straight into a std430 SSBO indexed by instance.
 * Size, color, angle and sheet frame are already evaluated from age.
 */
struct ParticleData {
    glm::vec4 positionSize;  ///< xyz = world position, w = half the billboard's width.
    glm::vec4 color;         ///< Linear RGBA, its intensity in the RGB.

    glm::vec4 params;        ///< x = edge softness (0 hard .. 1 soft); y = angle, radians; z = its texture,
                             ///< as 1 + its place in RenderView::SceneLists::particleTextures, 0 for the
                             ///< soft disc; w = 1 on the viewmodel layer.
    glm::vec4 uvRect;        ///< Its sheet frame: xy = the corner at the quad's (-1,-1), zw = the size.

    glm::vec4 trail;  ///< xyz = where it streaks to behind it, world space, zero for none; w = the share of
                      ///< its color an alpha particle gives off, or how far a distortion one bends the view.

    glm::vec4 uvNext;  ///< xy = the next sheet frame's corner, z = how far it has faded into it; w = its
                       ///< normal map, as params.z, 0 for none.

    /**
     * @brief The world box @p particle's quad can cover, as the vertex stage builds it.
     *
     * From its place to its streak's end, as far round them as a corner reaches turned any way.
     *
     * @param particle The billboard.
     * @return Its box.
     */
    static Math::AABB reachOf(const ParticleData& particle) {
        const glm::vec3 at(particle.positionSize);
        const glm::vec3 end = at + glm::vec3(particle.trail);
        const glm::vec3 reach(particle.positionSize.w * glm::root_two<float>());
        return {glm::min(at, end) - reach, glm::max(at, end) + reach};
    }
};

/**
 * @brief One point of a particle strip - a trail - as the vertex stage reads it.
 *
 * Each carries its strip's run and look, so one draw takes every strip of a texture.
 */
struct StripPoint {
    glm::vec4 positionWidth;  ///< xyz = world position, w = half the strip's width there.
    glm::vec4 color;          ///< Linear RGBA, its intensity in the RGB.

    glm::vec4 params;  ///< x = how far along its strip, 0 at its head to 1 at its tail; y, z = its strip's
                       ///< first and last point in RenderView::SceneLists::stripPoints.

    glm::vec4 look;  ///< x = the falloff across its width, as ParticleData::params.x; y = as
                     ///< ParticleData::trail.w; z = 1 on the viewmodel layer.

    /**
     * @brief The world box the strip can cover at @p point, as wide as it is there.
     *
     * @param point The point.
     * @return Its box.
     */
    static Math::AABB reachOf(const StripPoint& point) {
        const glm::vec3 at(point.positionWidth);
        const glm::vec3 reach(point.positionWidth.w);
        return {at - reach, at + reach};
    }
};

/**
 * @brief A run of StripPoints drawn as one camera-facing strip.
 */
struct ParticleStrip {
    uint32_t first   = 0;     ///< Its first point in RenderView::SceneLists::stripPoints.
    uint32_t count   = 0;     ///< Points it runs through, at least two.
    float    texture = 0.0f;  ///< Its texture, as ParticleData::params.z.
};

} // namespace Vkm::Engine
