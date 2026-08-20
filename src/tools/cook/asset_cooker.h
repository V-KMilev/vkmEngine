#pragma once

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Bakes assets from their in-memory (recipe-derived) form into the on-disk asset database.
 *
 * Editor-only (lives in vkm_cook).
 */
namespace AssetCooker {

/**
 * @brief Cook every non-hidden loaded asset into the library and cooked cache, then write the manifest.
 *
 * Writes per-asset recipe files (the library) plus cooked binary blobs (the
 * cooked cache). Meshes/textures get a binary `.vkmc`; materials get their
 * canonical inline descriptor as the library file (no binary cook needed). An
 * asset whose cooked output is already current (recipe hash unchanged, cooked
 * file readable by this build) is skipped. Existing manifest records for assets
 * not currently loaded are preserved, so the manifest accumulates the whole
 * project's asset database. Call before saving a scene (which then references
 * these assets by name only).
 *
 * Waits for any asset still importing off the ThreadPool first: one that has
 * not landed yet has nothing to bake, and baking around it would write a
 * manifest that promises files nobody produced.
 *
 * @param resources Resource manager whose loaded assets are cooked.
 * @return False when any asset failed to cook or the manifest failed to save,
 *         so an unattended build can tell a finished cook from a reported one.
 */
bool cookAllAssets(ResourceManager& resources);

} // namespace AssetCooker

} // namespace Vkm::Engine
