#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Vkm::Engine {

/**
 * @brief Shared cubemap capture basis.
 *
 * Rendering into a cube's faces must use the per-face basis GL samples with, so it lives here
 * once. Near/far is each baker's own, so only the 90deg convolution projection is shared.
 */
namespace GLCubemap {

/**
 * @brief The +face direction and its up vector, in GL cubemap face order (+X, -X, +Y, -Y, +Z, -Z).
 */
struct FaceBasis {
    glm::vec3 dir;
    glm::vec3 up;
};
inline const FaceBasis FACES[6] = {
    {{ 1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},
    {{-1.0f,  0.0f,  0.0f}, {0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  1.0f,  0.0f}, {0.0f,  0.0f,  1.0f}},
    {{ 0.0f, -1.0f,  0.0f}, {0.0f,  0.0f, -1.0f}},
    {{ 0.0f,  0.0f,  1.0f}, {0.0f, -1.0f,  0.0f}},
    {{ 0.0f,  0.0f, -1.0f}, {0.0f, -1.0f,  0.0f}},
};

/**
 * @brief The view matrix looking down face @p face from @p eye.
 *
 * @param face The cube face, 0-5 in GL's order.
 * @param eye  The probe position for a scene capture, the origin for a convolution.
 * @return The view matrix.
 */
inline glm::mat4 faceView(int face, const glm::vec3& eye) {
    return glm::lookAt(eye, eye + FACES[face].dir, FACES[face].up);
}

/**
 * @brief The 90deg, square projection a unit cube is convolved through.
 *
 * A scene capture uses its own near and far planes (see GLSceneCapture).
 *
 * @return The projection.
 */
inline glm::mat4 convolveProjection() {
    return glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
}

} // namespace GLCubemap

} // namespace Vkm::Engine
