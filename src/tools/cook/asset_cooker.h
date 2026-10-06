#pragma once

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Bakes assets from their in-memory (recipe-derived) form into the on-disk asset database.
 *
 * Lives in the vkm_cook library, which the runtime never links.
 */
namespace AssetCooker {

/**
 * @brief When a cook writes the binaries it bakes: before it returns, or after.
 *
 * Recording an asset (recipe file, manifest row) is cheap and makes its name resolve. Baking
 * takes seconds a texture, and nothing waits on it: a load whose cooked file is not current yet
 * falls back to the recipe.
 */
enum class Bake {
    Now,          ///< Everything written before the cook returns.
    InBackground  ///< Textures and meshes baked on the ThreadPool after it returns.
};

/**
 * @brief Cook every non-hidden loaded asset into the library and cooked cache, then write the manifest.
 *
 * Materials get their inline descriptor as the library file; the other kinds a binary `.vkmc`.
 * An asset whose output is current is skipped. Manifest records of unloaded assets are kept.
 * Call before saving a scene, which then names these assets only. Waits for in-flight imports.
 *
 * An asset's key hashes its source art once a session, while its version and recipe are
 * unchanged; art re-exported under a loaded asset is cookStaleAssets' to find. A background bake
 * is not repeated while running, deletes the previous artifact when it lands, and on failure is
 * logged and retried by the next cook; a queued one is dropped at exit.
 *
 * @param resources Resource manager whose loaded assets are cooked.
 * @param bake      Whether texture and mesh binaries are written before this returns; skeletons,
 *                  clips and sounds are copies, always written now.
 * @return False when any asset failed to cook or the manifest failed to save; a background
 *         bake's own failure is not in it.
 */
bool cookAllAssets(ResourceManager& resources, Bake bake = Bake::Now);

/**
 * @brief Re-bake every manifest entry whose cooked file no longer matches what it
 *        was baked from.
 *
 * An asset served from the cooked cache holds no recipe, so cookAllAssets never sees re-exported
 * art, a changed dependency or a COOKER_VERSION bump. Each record's recipe is hashed off disk;
 * one whose hash or base moved, or whose file is not current, is imported and baked again.
 * Reads all the project's source art: for an explicit cook only, never a save or open.
 *
 * @return False when a stale asset did not import or did not bake, or the
 *         manifest did not save.
 */
bool cookStaleAssets();

} // namespace AssetCooker

} // namespace Vkm::Engine
