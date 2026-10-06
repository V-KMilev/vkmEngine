#pragma once

#include <string>
#include <vector>

#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_handle.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class ResourceManager;
class Scene;

// Model import (glTF/GLB, FBX, OBJ): builds assets from what parseModel read. Each mesh (one
// material's triangles) is its own MeshAsset; the node graph becomes an entity hierarchy. Names
// come from ModelPartNames, so a re-import reproduces them and Mesh components re-link.

/**
 * @brief Build one of a model file's meshes, without blocking.
 *
 * Returns a loading stub at once; the parse runs on the ThreadPool and AsyncLoaderSystem fills
 * the asset a frame or more later. Idempotent by name, even while in flight.
 *
 * @param path The model file.
 * @param meshIndex Which of the file's meshes.
 * @param resources Receives the stub mesh.
 * @return Handle to the loading mesh; valid immediately, filled on a later frame.
 */
MeshHandle requestModelMeshAsync(const std::string& path, int meshIndex, ResourceManager& resources);

/**
 * @brief Re-extract one image a model file carries, by its reference in the file.
 *
 * What a "model-image" recipe is rebuilt from.
 *
 * @param path The model file.
 * @param ref The image's reference in the file: "*<index>" for one it carries.
 * @param usage What the texels mean, which decides how they are decoded and stored.
 * @param resources Receives the texture.
 * @return The texture, or an empty handle if @p path or @p ref don't resolve.
 */
TextureHandle loadModelEmbeddedTexture(
    const std::string& path,
    const std::string& ref,
    TextureUsage usage,
    ResourceManager& resources
);

/**
 * @brief Build and register the rig a model file's skinned meshes are bound to.
 *
 * One per file: every joint the meshes name plus the nodes joining them from their lowest common
 * ancestor, depth-first so a parent precedes its bones. Two disjoint rigs are refused, not
 * merged under an invented root. Idempotent by name; synchronous.
 *
 * @param path The model file.
 * @param resources Receives the skeleton.
 * @return The built or existing skeleton, or an empty handle when the file has no bones or its
 *         rig cannot be resolved.
 */
SkeletonHandle loadModelSkeleton(const std::string& path, ResourceManager& resources);

/**
 * @brief Build and register one of a model file's animations, bound to a rig.
 *
 * Idempotent by name. A channel naming a node the rig lacks is dropped and counted. Markers come
 * from the recipe and are written back into the clip's source; one unnamed or outside the clip
 * is dropped and counted.
 *
 * @param path The model file.
 * @param clipIndex Which of the file's clips.
 * @param markers Authored markers to carry on the clip; may be empty.
 * @param resources Receives the clip (and the rig it names).
 * @param rig An already-loaded skeleton to bind against, for an animation exported without a
 *        mesh; empty binds to @p path's own rig.
 * @return The built or existing clip, or an empty handle on failure.
 */
AnimationClipHandle loadModelAnimationClip(
    const std::string& path,
    int clipIndex,
    std::vector<ClipMarker> markers,
    ResourceManager& resources,
    const std::string& rig = {}
);

/**
 * @brief What an import produced.
 *
 * An empty root is not a failure: a clips-only file spawns nothing. `ok` tells them apart.
 */
struct ModelImport {
    EntityId root{};      ///< Empty when nothing was spawned.
    uint32_t clips = 0;   ///< Animation clips imported.
    bool     ok = false;  ///< Whether the file opened and was understood.
};

/**
 * @brief Import a whole model file into @p scene.
 *
 * Adds the assets (idempotent by name), then spawns a root and an entity per node that is not
 * only a bone. A node's one unskinned mesh goes on its entity; other meshes get child entities.
 * A rigged file gets one Animator, on the entity the bones are composed in (the root bone node's
 * parent, or the import root); SkeletalAnimationSystem poses it and everything under it.
 *
 * @param path The model file.
 * @param resources Receives the imported assets.
 * @param scene Scene the hierarchy is spawned into.
 * @return What the import produced.
 */
ModelImport importModelIntoScene(const std::string& path, ResourceManager& resources, Scene& scene);

} // namespace Vkm::Engine
