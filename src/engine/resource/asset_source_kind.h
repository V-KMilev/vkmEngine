#pragma once

#include <nlohmann/json_fwd.hpp>

#include "resource/texture_format.h"

namespace Vkm::Engine {

/**
 * @brief The `kind` field of an asset's source descriptor.
 *
 * The `kind` picks the import that rebuilds an asset from its recipe. The strings are
 * the file format: changing one changes what existing assets say.
 */
namespace AssetSourceKind {
    /// What an asset read from its cooked file carries instead of its recipe.
    inline constexpr const char* COOKED    = "cooked";
    /// A mesh derived from another by simplification (an LOD).
    inline constexpr const char* DECIMATE  = "decimate";
    inline constexpr const char* FILE      = "file";       ///< One file on disk, decoded as-is.
    inline constexpr const char* GENERATOR = "generator";  ///< A primitive built by the mesh generators.
    /// A material whose values live in the descriptor itself.
    inline constexpr const char* INLINE    = "inline";
    inline constexpr const char* MODEL     = "model";      ///< One index into a glTF, FBX or OBJ file.
    /// A texture embedded in, or referenced beside, a model file.
    inline constexpr const char* MODEL_IMAGE = "model-image";
    inline constexpr const char* SOLID     = "solid";      ///< A single-colour texture.

} // namespace AssetSourceKind

/**
 * @brief Keys a source descriptor is written with in one file and read by in another.
 *
 * A key spelled two ways reads as absent. The strings are the file format.
 */
namespace AssetSourceKey {
    /// What a texture's texels mean, a TextureUsage name.
    inline constexpr const char* USAGE            = "usage";
    /// The source file an import reads, project-relative.
    inline constexpr const char* PATH             = "path";
    inline constexpr const char* MESH             = "mesh";             ///< Which of a model file's meshes.
    /// Which of a model file's animations.
    inline constexpr const char* CLIP             = "clip";
    /// The rig of another file a clip is bound to, by name.
    inline constexpr const char* RIG              = "rig";
    /// A clip's authored {name, time} markers.
    inline constexpr const char* MARKERS          = "markers";
    inline constexpr const char* REF              = "ref";              ///< Which of a model file's images.
    /// The mesh a decimated level is derived from, by name.
    inline constexpr const char* BASE             = "base";
    /// A decimated level's share of its base's triangles.
    inline constexpr const char* RATIO            = "ratio";
    /// Whether a texture builds a mip chain.
    inline constexpr const char* GENERATE_MIPMAPS = "generateMipmaps";
} // namespace AssetSourceKey

/**
 * @brief Read the usage a texture recipe's `usage` key names.
 *
 * An absent key reads as Data. An unknown usage reads as Data too, through
 * reportError.
 *
 * @param source JSON source descriptor for a texture.
 * @return The usage the key names, or TextureUsage::Data.
 */
TextureUsage textureUsageFromRecipe(const nlohmann::json& source);

} // namespace Vkm::Engine
