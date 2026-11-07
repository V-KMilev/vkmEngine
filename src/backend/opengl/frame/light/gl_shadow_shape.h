#pragma once

#include <glm/glm.hpp>

namespace Vkm::Engine {

struct LightData;

inline constexpr float SPOT_MAP_MARGIN = 1.1f;              ///< A spot's map is drawn this much wider than
                                                            ///< its cone, so a filter disk at the cone's edge
                                                            ///< stays on it.
inline const float SPOT_MAP_WIDEST = glm::radians(170.0f);  ///< The widest a spot's one map is drawn: wider,
                                                            ///< a perspective map degenerates.

/**
 * @brief Whether @p light is a spot whose cone, with the margin, is wider than one map holds.
 *
 * Such a spot casts through a cube, as a point light does (GpuLight.bake.y), so its rim is shadowed.
 *
 * @param light A light of the frame.
 * @return True for a spot too wide for one map.
 */
bool spotCastsThroughCube(const LightData& light);

/**
 * @brief Whether @p light's shadow is drawn into a cube: a point light, or a spot too wide for one map.
 *
 * @param light A light of the frame.
 * @return True for a light whose shadow takes a cube.
 */
bool castsThroughCube(const LightData& light);

/**
 * @brief The field of view of the one map of spot @p light: its cone with the margin.
 *
 * Never zero, so a zero cone's projection does not divide by tan(0).
 *
 * @param light A spot of the frame that does not cast through a cube.
 * @return Radians, at most SPOT_MAP_WIDEST.
 */
float spotMapFov(const LightData& light);

} // namespace Vkm::Engine
