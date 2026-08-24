#pragma once

#include <cstdint>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The asset kinds that live in the library (shaders stay source-referenced and
 * are not part of the cooked database).
 *
 * The vocabulary rather than the database: this names what kinds of asset a
 * project holds, and AssetLibrary is one user of it. Keeping it here, beside
 * the assets it names, is what lets code that needs only the tag say which kind
 * without pulling in the manifest, its map and <filesystem> with it.
 *
 * Append new kinds before Count. Nothing on disk carries the numeric value -
 * the manifest and the scene write the name, and a cooked file carries its own
 * kind tag - but TYPE_DIRS in asset_library.cpp is indexed by it, and a
 * static_assert there ties the two together.
 */
enum class AssetType : uint8_t {
    Mesh,
    Texture,
    Material,
    Skeleton,
    AnimationClip,
    AudioClip,

    Count
};

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::AssetType, "mesh", "texture", "material", "skeleton", "animationClip", "audioClip")
