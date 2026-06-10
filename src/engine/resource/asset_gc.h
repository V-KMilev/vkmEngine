#pragma once

#include <cstddef>

namespace Engine {

class Scene;
class ResourceManager;

/**
 * @brief Free scene-owned assets no live entity reaches (mark-and-sweep).
 *
 * Walks the scene's asset references (every Mesh::mesh,
 * Mesh::material, and each surviving material's texture handles) to build the
 * reachable set, then removes every mesh / material / texture asset that is
 * neither reachable nor `pinned` (engine-owned: shaders, builtins, editor
 * previews). Removal goes through ResourceManager::remove, which bumps the
 * global version, so the backend drops + frees the corresponding GPU buffers
 * on its next sync - this is what reclaims the memory of a deleted model.
 *
 * Deliberate, explicit operation (editor "Purge Unused"): deleting an entity
 * is undoable and intentionally leaves its assets resident so a redo / re-use
 * still has them. Freeing assets makes any earlier undo snapshot that named
 * them dangle, so the caller should clear the undo history after purging.
 *
 * @return number of assets removed across all three types.
 */
std::size_t purgeUnusedAssets(Scene& scene, ResourceManager& resources);

} // namespace Engine
