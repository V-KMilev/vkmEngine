#pragma once

namespace Vkm::Engine {

/**
 * @brief The `kind` field of an asset's source descriptor.
 *
 * Every asset carries a small JSON document saying where it came from, and its
 * `kind` is what picks the factory that rebuilds it on the next load. Eleven
 * kinds exist, written in one file and matched in another - `asset_registration.cpp`
 * and `recipe_registration.cpp` read what the loaders and generators write - and
 * as bare literals on both sides a typo produced an asset nothing could rebuild
 * and a log line naming a kind that does not exist.
 *
 * Named here so the two halves cannot disagree. The strings are the file format,
 * so they are what they are: changing one changes what an existing project's
 * assets say about themselves.
 */
namespace AssetSourceKind {
    inline constexpr const char* BUILTIN   = "builtin";    ///< A named engine texture (white, black, normal, gray).
    inline constexpr const char* COOKED    = "cooked";     ///< A baked artifact under the project's cooked/.
    inline constexpr const char* DECIMATE  = "decimate";   ///< A mesh derived from another by simplification (an LOD).
    inline constexpr const char* DEFAULT   = "default";    ///< The engine's fallback material.
    inline constexpr const char* FILE      = "file";       ///< One file on disk, decoded as-is.
    inline constexpr const char* FOLDER    = "folder";     ///< A PBR material assembled from a directory of maps.
    inline constexpr const char* GENERATOR = "generator";  ///< A primitive built by the mesh generators.
    inline constexpr const char* INLINE    = "inline";     ///< A material whose values live in the descriptor itself.
    inline constexpr const char* MODEL     = "model";      ///< One index into a model file Assimp parses.
    /// A texture embedded in, or referenced beside, a model file.
    inline constexpr const char* MODEL_IMAGE = "model-image";
    inline constexpr const char* SOLID     = "solid";      ///< A single-colour texture.
} // namespace AssetSourceKind

} // namespace Vkm::Engine
