#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Vkm::Engine {

/**
 * @brief How a debug drawing meets the scene in front of it.
 */
enum class DebugDepth : uint8_t {
    Ghosted,  ///< Full where seen, faint and dotted where the scene hides it
    Hidden,   ///< Only where seen
    OnTop,    ///< Full over everything: what must not be lost behind the scene

    Count  ///< Number of modes. Keep last.
};

/**
 * @brief One segment as the backend draws it: DebugDraw's record.
 *
 * Laid out as shaders/opengl/overlay/debug_draw reads it, std430.
 */
struct alignas(16) DebugLine {
    glm::vec3 a{0.0f};       ///< Its start
    float     width = 0.0f;  ///< Points
    glm::vec3 b{0.0f};       ///< Its end. An end on the start is a round dot
    uint32_t  color = 0;     ///< Display RGBA8, red in the low byte
    uint32_t  flags = 0;     ///< DebugDepth in bits 0-1
};
static_assert(sizeof(DebugLine) == 48, "DebugLine is shaders/opengl/overlay/debug_draw's record");

} // namespace Vkm::Engine
