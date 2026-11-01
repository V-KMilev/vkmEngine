#pragma once

namespace Vkm::Engine {

/**
 * @brief The `kind` field of an asset's source descriptor.
 *
 * The `kind` picks the import that rebuilds an asset from its recipe; a material's
 * recipe carries none, being its values themselves. The strings are the file format:
 * changing one changes what existing assets say.
 */
namespace AssetSourceKind {
    inline constexpr const char* COOKED    = "cooked";     ///< What an asset read from its cooked file
                                                           ///< carries instead of its recipe.
    inline constexpr const char* DECIMATE  = "decimate";   ///< A mesh derived from another by simplification
                                                           ///< (an LOD).
    inline constexpr const char* FILE      = "file";       ///< One file on disk, decoded as-is.
    inline constexpr const char* GENERATOR = "generator";  ///< A primitive built by the mesh generators.
    inline constexpr const char* MODEL     = "model";      ///< One index into a glTF, FBX or OBJ file.
    inline constexpr const char* MODEL_IMAGE = "model-image";  ///< A texture embedded in, or referenced
                                                               ///< beside, a model file.
    inline constexpr const char* SOLID     = "solid";      ///< A single-color texture.

} // namespace AssetSourceKind

/**
 * @brief Keys a source descriptor is written with in one file and read by in another.
 *
 * A key spelled two ways reads as absent. The strings are the file format.
 */
namespace AssetSourceKey {
    inline constexpr const char* KIND             = "kind";   ///< Which import rebuilds the asset, an
                                                              ///< AssetSourceKind string.
    inline constexpr const char* USAGE            = "usage";  ///< What a texture's texels mean, a
                                                              ///< TextureUsage name.
    inline constexpr const char* PATH             = "path";   ///< The source file an import reads,
                                                              ///< project-relative.
    inline constexpr const char* MESH             = "mesh";   ///< Which of a model file's meshes.
    inline constexpr const char* CLIP             = "clip";   ///< Which of a model file's animations.
    inline constexpr const char* RIG              = "rig";    ///< The rig of another file a clip is bound to,
                                                              ///< by name.
    inline constexpr const char* MARKERS          = "markers";  ///< A clip's authored {name, time} markers.
    inline constexpr const char* REF              = "ref";    ///< Which of a model file's images.
    inline constexpr const char* BASE             = "base";   ///< The mesh a decimated level is derived from,
                                                              ///< by name.
    inline constexpr const char* RATIO            = "ratio";  ///< A decimated level's share of its base's
                                                              ///< triangles.
    inline constexpr const char* GENERATE_MIPMAPS = "generateMipmaps";
        ///< Whether a texture builds a mip chain.
    inline constexpr const char* WRAP             = "wrap";   ///< A texture's wrap mode, when it does not
                                                              ///< clamp (textureWrapFromRecipe).
    inline constexpr const char* FILTER           = "filter";  ///< A texture's filter override, when it has
                                                               ///< one (textureFilterFromRecipe).
    inline constexpr const char* TYPE             = "type";   ///< Which primitive a generator builds
                                                              ///< ("cube", "sphere").
    inline constexpr const char* PARAMS           = "params";  ///< A generator's parameters, an object.
    inline constexpr const char* COLOR            = "color";  ///< A solid texture's one color, RGBA.
    inline constexpr const char* ROUGHNESS_NORMAL = "roughnessNormal";
        ///< On a metallic-roughness map: its material's normal map's source file, project-relative, whose
        ///< detail the cook folds into the roughness its mips keep (AssetCooker::bakeTexture).
} // namespace AssetSourceKey

} // namespace Vkm::Engine
