#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Vkm::Engine {

/**
 * @brief Shared cubemap capture basis for the IBL + reflection-probe bakers.
 *
 * Both bakers render or convolve a scene into the six faces of a cube, and both
 * must use the same per-face direction / up basis the forward + skybox shaders
 * sample - so it lives here once instead of being copied into each baker. The
 * capture projection's near/far is the baker's own concern (a scene capture
 * reaches far; a unit-cube convolution does not), so only the 90deg convolution
 * projection is shared.
 */
namespace GLCubemap {

/**
 * @brief The +face direction and its up vector, in GL cubemap face order (+X, -X, +Y,
 * -Y, +Z, -Z).
 */
struct FaceBasis { glm::vec3 dir; glm::vec3 up; };
inline const FaceBasis FACES[6] = {
    {{ 1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},
    {{-1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  1.0f,  0.0f}, {0.0f,  0.0f,  1.0f}},
    {{ 0.0f, -1.0f,  0.0f}, {0.0f,  0.0f, -1.0f}},
    {{ 0.0f,  0.0f,  1.0f}, {0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  0.0f, -1.0f}, {0.0f, -1.0f,  0.0f}},
};

/**
 * @brief View matrix looking down face @p face from @p eye (eye = the probe position
 * for a scene capture, the origin for a direction-only convolution).
 */
inline glm::mat4 faceView(int face, const glm::vec3& eye) {
    return glm::lookAt(eye, eye + FACES[face].dir, FACES[face].up);
}

/**
 * @brief 90deg fov, aspect-1 projection for convolving a unit cube (irradiance /
 * prefilter / equirect projection). Scene captures use their own far plane.
 */
inline glm::mat4 convolveProjection() {
    return glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
}

} // namespace GLCubemap

} // namespace Vkm::Engine
