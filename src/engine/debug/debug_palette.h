#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief What a debug drawing shows: each kind has a toggle (DebugDraw::setShown) and a color
 *        (DebugPalette::of).
 *
 * General is gameplay's and tools' own, shown unless a host turns it off; the engine draws the
 * others while a host shows them.
 */
enum class DebugCategory : uint8_t {
    General,
    Colliders,  ///< Colliders and joints
    Contacts,   ///< Where the last tick's bodies touched, and how hard the solver pushed each
    Audio,      ///< Sources' reach and listeners' facing
    Acoustics,  ///< The ear's rays: the room's, off what they strike, and those to each sound
    Lights,
    Probes,     ///< Reflection probes, planar reflectors and irradiance volumes
    Cameras,
    Skeletons,
    Effects,    ///< Decals and particle emitters
    Bounds,     ///< Every drawn mesh's world box
    Selection,  ///< The focus's meshes' world boxes (DebugDraw::setFocus)

    Count  ///< Number of categories. Keep last.
};

/**
 * @brief The colors debug drawings are made in, one owner for the engine's lines and a host's
 *        markers alike: the editor's gizmo colors.
 */
namespace DebugPalette {

/**
 * @brief A color as the display shows it: 8-bit sRGB and alpha.
 *
 * Plain bytes, as Math::AxisColor is, so a host's style can build constants on it.
 */
struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    /// As a DebugStyle holds a color: each channel 0 to 1.
    glm::vec4 value() const {
        return glm::vec4(r, g, b, a) / 255.0f;
    }

    /// The same, @p share of it present: the focus's others, a far reach, what takes no part.
    constexpr Color faded(float share) const {
        return {r, g, b, static_cast<uint8_t>(static_cast<float>(a) * share + 0.5f)};
    }
};

inline constexpr Color HIGHLIGHT{255, 210, 50, 255};          ///< The focus: the editor's selection.
inline constexpr Color SECONDARY = HIGHLIGHT.faded(0.51f);    ///< The focus's others, behind its primary.
inline constexpr Color JOINT{255, 170, 60, 220};              ///< Amber, against the colliders' green.
inline constexpr Color DECAL{200, 120, 220, 200};             ///< Violet, against the emitters' amber.
inline constexpr Color IRRADIANCE_VOLUME{235, 150, 77, 200};  ///< Orange, against the probes' blue.
inline constexpr Color OCCLUDED{255, 96, 72, 230};            ///< A ray to a sound, up to what stops it.

/// A category's color.
constexpr Color of(DebugCategory category) {
    switch (category) {
        case DebugCategory::General:   return {235, 235, 235, 230};
        case DebugCategory::Colliders: return {80, 220, 120, 200};
        case DebugCategory::Contacts:  return {255, 72, 72, 230};
        case DebugCategory::Audio:     return {232, 62, 208, 220};
        case DebugCategory::Acoustics: return {255, 150, 236, 210};
        case DebugCategory::Lights:    return {255, 214, 140, 210};  // a light draws in its own color
        case DebugCategory::Probes:    return {77, 158, 235, 200};
        case DebugCategory::Cameras:   return {120, 200, 220, 220};
        case DebugCategory::Skeletons: return {120, 190, 255, 230};
        case DebugCategory::Effects:   return {240, 200, 90, 200};
        case DebugCategory::Bounds:    return {230, 200, 60, 160};
        case DebugCategory::Selection: return HIGHLIGHT;
        case DebugCategory::Count:     break;
    }
    return {};
}

/// The audio color, faded: what is heard quietly or not at all.
inline constexpr Color QUIET_AUDIO = of(DebugCategory::Audio).faded(0.36f);

/// What a light draws in: its own color, a little dimmed.
inline Color ofLight(const glm::vec3& color) {
    const glm::vec3 c = glm::clamp(color, 0.0f, 1.0f) * 220.0f;
    return {static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g), static_cast<uint8_t>(c.b), 200};
}

} // namespace DebugPalette

} // namespace Vkm::Engine

VKM_ENUM_NAMES(
    ::Vkm::Engine::DebugCategory,
    "General",
    "Colliders",
    "Contacts",
    "Audio",
    "Acoustics",
    "Lights",
    "Probes",
    "Cameras",
    "Skeletons",
    "Effects",
    "Bounds",
    "Selection"
)
