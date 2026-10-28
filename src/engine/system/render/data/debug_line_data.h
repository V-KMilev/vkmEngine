#pragma once

#include <cstdint>
#include <vector>

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
 * Laid out as shaders/debug reads it, std430.
 */
struct DebugLine {
    glm::vec4 a{0.0f};       ///< xyz its start; w how far along its path the start is, metres
    glm::vec4 b{0.0f};       ///< xyz its end; w likewise. An end on the start is a round dot
    glm::vec4 pulse{0.0f};   ///< DebugPulse's dash, spacing, speed and phase; spacing 0 is solid
    uint32_t  color = 0;     ///< Display RGBA8, red in the low byte
    float     width = 0.0f;  ///< Points
    float     glow  = 0.0f;  ///< Brightness of its glow, displayed units; see DebugStyle::glowing
    uint32_t  flags = 0;     ///< DebugDepth in bits 0-1; the pulse's trail, 0 to 255, in bits 8-15
};
static_assert(sizeof(DebugLine) == 64, "DebugLine is shaders/debug's record");

/**
 * @brief What the backend draws of a frame's DebugDraw (RenderView::debug).
 */
struct DebugFrame {
    const std::vector<DebugLine>* lines = nullptr;  ///< Borrowed; null or empty draws nothing
    double seconds    = 0.0;   ///< The pulses' clock: wall seconds, running in a paused game too
    float  pointScale = 1.0f;  ///< Pixels a point: the display's density by the host's scale
};

} // namespace Vkm::Engine
