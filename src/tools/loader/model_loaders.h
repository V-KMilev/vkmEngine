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

/**
 * @brief Model import via Assimp (glTF/glb, OBJ, FBX, DAE, STL, PLY, 3DS).
 *
 * Assimp is the single import path. Each aiMesh becomes its own MeshAsset
 * (the engine's MeshAsset is single-submesh); the aiNode graph becomes an
 * entity hierarchy under one root entity.
 *
 * Naming is deterministic so a re-import (or a scene reload through the
 * recipe "model" kind) reproduces the same asset names and the
 * Mesh components re-link by name:
 *   mesh     "<stem>:mesh<index>"
 *   material "<stem>:mat<index>"   (or "<stem>:mat_default")
 *   skeleton "<stem>:skeleton"     (one rig per file)
 *   clip     "<stem>:clip<index>"
 * where <stem> is the model file name without extension and the indices
 * are Assimp's global mesh / material / animation indices.
 */

/**
 * @brief Build one aiMesh's geometry from a model file, without blocking.
 *
 * Registers a stub MeshAsset with loading=true, posts the Assimp parse +
 * vertex extraction to ThreadPool, and returns the handle immediately.
 * AsyncLoaderSystem patches the live asset with the decoded vertices,
 * indices and bounds 1+ frames out.
 *
 * Idempotent by name (derived from path+meshIndex): a second request for
 * the same (path, meshIndex) returns the existing handle even while the
 * first is still in flight.
 * VisibilitySystem already skips meshes with zero-extent bounds, so a
 * loading mesh stays invisible (no fallback geometry needed).
 *
 * @param path Path to the model file to parse with Assimp.
 * @param meshIndex Assimp global mesh index to extract.
 * @param resources Resource manager the stub mesh is registered with.
 * @return Handle to the loading mesh; valid immediately, filled on a later frame.
 */
MeshHandle requestModelMeshAsync(
    const std::string& path,
    int meshIndex,
    ResourceManager& resources
);

/**
 * @brief Build and register one material from a model file, loading its textures.
 *
 * Idempotent by name. A @p materialIndex < 0 yields a default material.
 *
 * @param path Path to the model file to parse with Assimp.
 * @param materialIndex Assimp global material index, or < 0 for a default material.
 * @param resources Resource manager the material (and its textures) is added to.
 * @return Handle to the built or existing material.
 */
MaterialHandle loadModelMaterial(
    const std::string& path,
    int materialIndex,
    ResourceManager& resources
);

/**
 * @brief Re-extract one embedded texture from a model file by Assimp ref.
 *
 * Decoded with the engine's flip convention. Used by the "model-image"
 * texture factory to round-trip embedded textures through scene save/load -
 * the pixels live in the model file, not in the scene JSON.
 *
 * @param path Path to the model file containing the embedded image.
 * @param ref Assimp reference identifying the embedded texture within the file.
 * @param srgb Whether to decode the image into an sRGB texture.
 * @param resources Resource manager the extracted texture is added to.
 * @return Handle to the extracted texture, or an empty handle if @p path or
 *         @p ref don't resolve.
 */
TextureHandle loadModelEmbeddedTexture(
    const std::string& path,
    const std::string& ref,
    bool srgb,
    ResourceManager& resources
);

/**
 * @brief Build and register the rig a model file's skinned meshes are bound to.
 *
 * One skeleton per file, named "<stem>:skeleton": the union of every bone named
 * by any of the file's meshes, plus their ancestor nodes down from the joint
 * that is common to all of them, emitted depth-first so a bone's parent always
 * precedes it. A file holding two disjoint rigs is refused rather than merged
 * into one with an invented shared root.
 *
 * Idempotent by name. Synchronous: a rig is small enough that the async path
 * would cost more than it saves.
 *
 * @param path Path to the model file to parse with Assimp.
 * @param resources Resource manager the skeleton is added to.
 * @return Handle to the built or existing skeleton, or an empty handle when the
 *         file has no bones or its rig cannot be resolved.
 */
SkeletonHandle loadModelSkeleton(const std::string& path, ResourceManager& resources);

/**
 * @brief Build and register one of a model file's animations, bound to a rig.
 *
 * Named "<stem>:clip<index>" by Assimp's global animation index, so a re-import
 * relinks, and idempotent by that name. Channels are resolved to bone indices
 * here; one naming a node the rig does not hold is dropped and counted.
 *
 * The rig is the one in the same file unless @p rig names another. It has to be
 * nameable, because a rig is built from skin weights and an animation exported
 * without a mesh has none of its own - which is how animation libraries are
 * distributed, one file per motion against a rig sent once.
 *
 * Markers are authored rather than imported, so they arrive from the recipe and
 * are written back into the clip's source descriptor for the next cook. One
 * named nothing, or timed outside the clip, is dropped and counted.
 *
 * @param path Path to the model file to parse with Assimp.
 * @param clipIndex Assimp global animation index to extract.
 * @param markers Authored markers to carry on the clip; may be empty.
 * @param resources Resource manager the clip (and the rig it names) is added to.
 * @param rig Name of an already-loaded skeleton to bind against. Empty binds to
 *        the rig in @p path's own file, which is what a skinned export carries.
 * @return Handle to the built or existing clip, or an empty handle on failure.
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
 * A root of nothing is not a failure on its own: a file carrying clips and no
 * mesh imports successfully and spawns no entities, which is what a downloaded
 * animation is. `ok` is what separates the two.
 */
struct ModelImport {
    EntityId root{};      ///< Root of the spawned subtree; empty when none was spawned.
    uint32_t clips = 0;   ///< Animation clips imported from the file.
    bool     ok = false;  ///< Whether the file opened and was understood.
};

/**
 * @brief Import a whole model file into @p scene.
 *
 * Adds every aiMesh's MeshAsset + MaterialAsset to @p resources (idempotent
 * by name), plus the file's skeleton and animation clips when it is rigged,
 * then spawns a root entity with one child entity per aiNode
 * (Transform from the node matrix, a Mesh component per referenced mesh),
 * preserving the node hierarchy.
 *
 * A rigged file also gets one Animator, on the entity whose frame the rig's
 * bones are composed in - the parent of the root bone's node, or the import
 * root when the rig is rooted at the scene node itself. That entity is the rig:
 * SkeletalAnimationSystem poses it and everything under it.
 *
 * A file with clips and no mesh is a successful import that spawns nothing -
 * a downloaded animation is exactly that - so the outcome is reported rather
 * than encoded in the root: an empty root and `ok` both mean "no subtree", and
 * only one of them is a failure.
 *
 * @param path Path to the model file to import.
 * @param resources Resource manager the imported meshes/materials are added to.
 * @param scene Scene the entity hierarchy is spawned into.
 * @return What the import produced.
 */
ModelImport importModelIntoScene(
    const std::string& path,
    ResourceManager& resources,
    Scene& scene
);

} // namespace Vkm::Engine
