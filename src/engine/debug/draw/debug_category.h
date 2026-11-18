#pragma once

#include <cstdint>

#include "base/reflect/enum_names.h"

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
    Probes,     ///< Reflection probes and irradiance volumes
    Cameras,
    Skeletons,
    Effects,    ///< Decals and particle emitters
    Bounds,     ///< Every drawn mesh's world box
    Selection,  ///< The focus's meshes' world boxes (DebugDraw::setFocus)

    Count  ///< Number of categories. Keep last.
};

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
