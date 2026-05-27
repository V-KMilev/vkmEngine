#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "system/visibility/culling/occlusion_oracle.h"
#include "system/visibility/visibility_context.h"

namespace Engine {

/**
 * @brief Occlusion culling against the Hi-Z (max-Z) pyramid mirror.
 *
 * Tests an AABB's screen-space footprint against the previous frame's
 * pyramid published by @ref OcclusionOracle:
 *
 *   1. Project all 8 corners with the oracle's viewProj.
 *   2. Bail (return visible) if any corner has w <= 0 - that's a corner
 *      behind the camera plane and the test isn't safe.
 *   3. Take the screen-space bounds of the projected corners.
 *   4. Take the AABB's NEAREST distance-from-camera (-view.z over all
 *      corners) - the most conservative test point.
 *   5. Sample the pyramid over the screen bounds and take the MAX. The
 *      pyramid stores max(view distance) per cell, so the max over the
 *      footprint is the FARTHEST anything visible covers it from.
 *   6. AABB is occluded iff its nearest point is FARTHER than the
 *      pyramid's max distance for its footprint.
 *
 * One-frame stale: the oracle was published at the end of the previous
 * frame. A teleport / scene swap invalidates the oracle so we don't
 * cull against a stale view. Defaults to returning visible (no occluder
 * data) so failures are conservative.
 */
namespace OcclusionCuller {

inline bool isVisible(
    const glm::vec3& boundsMin,
    const glm::vec3& boundsMax,
    const VisibilityContext& /*context*/
) {
    const auto frame = OcclusionOracle::get().snapshot();
    if (!frame.ready) return true;

    const glm::vec3 c[8] = {
        {boundsMin.x, boundsMin.y, boundsMin.z},
        {boundsMax.x, boundsMin.y, boundsMin.z},
        {boundsMin.x, boundsMax.y, boundsMin.z},
        {boundsMax.x, boundsMax.y, boundsMin.z},
        {boundsMin.x, boundsMin.y, boundsMax.z},
        {boundsMax.x, boundsMin.y, boundsMax.z},
        {boundsMin.x, boundsMax.y, boundsMax.z},
        {boundsMax.x, boundsMax.y, boundsMax.z},
    };

    float minU = 1.0e9f, minV = 1.0e9f, maxU = -1.0e9f, maxV = -1.0e9f;
    float nearestDistance = 1.0e9f;  // smallest -view.z = closest to camera

    for (int i = 0; i < 8; ++i) {
        const glm::vec4 viewSpace = frame.view * glm::vec4(c[i], 1.0f);
        const float distance = -viewSpace.z;
        if (distance < nearestDistance) nearestDistance = distance;

        const glm::vec4 clip = frame.viewProj * glm::vec4(c[i], 1.0f);
        // A corner crossing the near plane (w near zero) makes the divide
        // unstable and the screen-space bounds wrong; punt to visible.
        if (clip.w <= 0.01f) return true;
        const float invW = 1.0f / clip.w;
        const float u = (clip.x * invW) * 0.5f + 0.5f;
        const float v = (clip.y * invW) * 0.5f + 0.5f;
        if (u < minU) minU = u;
        if (u > maxU) maxU = u;
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    // Any corner is in front of the camera, so the AABB CAN'T be entirely
    // behind it. Conservatively: a negative nearest distance means the
    // AABB straddles the camera plane - punt to visible.
    if (nearestDistance <= 0.0f) return true;

    // Bail when the AABB straddles outside the [0,1] screen rect - the
    // frustum culler should have handled this, but if a corner clips out
    // we'd index outside the pyramid.
    if (maxU <= 0.0f || maxV <= 0.0f || minU >= 1.0f || minV >= 1.0f) {
        return true;  // off-screen; let frustum culler decide.
    }
    minU = std::max(minU, 0.0f);
    minV = std::max(minV, 0.0f);
    maxU = std::min(maxU, 1.0f);
    maxV = std::min(maxV, 1.0f);

    const int w = static_cast<int>(frame.width);
    const int h = static_cast<int>(frame.height);
    int x0 = static_cast<int>(std::floor(minU * (w - 1)));
    int x1 = static_cast<int>(std::ceil (maxU * (w - 1)));
    int y0 = static_cast<int>(std::floor(minV * (h - 1)));
    int y1 = static_cast<int>(std::ceil (maxV * (h - 1)));
    x0 = std::max(0, std::min(x0, w - 1));
    x1 = std::max(0, std::min(x1, w - 1));
    y0 = std::max(0, std::min(y0, h - 1));
    y1 = std::max(0, std::min(y1, h - 1));

    // Walk the cells the AABB covers. With mip 4 + a 1080p viewport this
    // is ~4x2 cells for typical mid-distance objects - cheap. For a huge
    // AABB it can climb; that's the conservative bound and acceptable
    // since the frustum culler is the primary reject.
    float pyramidMax = 0.0f;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float v = frame.pyramid[y * w + x];
            if (v > pyramidMax) pyramidMax = v;
        }
    }

    // pyramidMax == 0 indicates the pyramid was cleared / sky (no opaque
    // geometry covered the footprint). Can't occlude against that.
    if (pyramidMax <= 0.0f) return true;

    // The classic Hi-Z conservative test: if the AABB's nearest distance
    // is further than the farthest opaque pixel in its footprint, every
    // covered pixel hides it.
    return nearestDistance <= pyramidMax;
}

} // namespace OcclusionCuller

} // namespace Engine
