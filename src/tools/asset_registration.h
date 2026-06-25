#pragma once

namespace Engine {

/**
 * @brief Register the asset factory "kinds" the AssetFactories registry
 *        dispatches on at scene load. Split by where they run.
 *
 * The Engine namespace (engine/io/) owns the registry; these functions (in
 * tools/) wire up the concrete generators/loaders, keeping the engine layer
 * free of direct dependencies on tools/.
 */

/**
 * @brief Runtime + editor. Kinds that load from the cooked asset database with no
 * Assimp and no image decode: cooked meshes/textures, inline materials (loaded
 * from the library), and the engine's directory-based shaders. Call once at
 * startup before any scene I/O.
 */
void registerCookedAssetFactories();

/**
 * @brief Editor only. The heavy recipe kinds that (re)produce assets from their
 * source: procedural mesh generators, Assimp model import, and file/solid/
 * folder textures and materials. These are what the cooker runs to populate
 * the cooked cache; a runtime build does not link them. Call after
 * registerCookedAssetFactories().
 */
void registerRecipeAssetFactories();

} // namespace Engine
