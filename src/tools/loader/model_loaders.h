#pragma once

#include <string>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_handle.h"
#include "ecs/entity.h"

namespace Engine {

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
 * where <stem> is the model file name without extension and the indices
 * are Assimp's global mesh / material indices.
 */

/**
 * @brief Build one aiMesh's geometry from a model file.
 *
 * Sets name + source on the asset so it round-trips through scene save/load.
 *
 * @param path Path to the model file to parse with Assimp.
 * @param meshIndex Assimp global mesh index to extract.
 * @return The built MeshAsset, or an empty MeshAsset on failure.
 */
MeshAsset loadModelMesh(const std::string& path, int meshIndex);

/**
 * @brief Non-blocking variant of loadModelMesh.
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
 * @brief Import a whole model file into @p scene.
 *
 * Adds every aiMesh's MeshAsset + MaterialAsset to @p resources (idempotent
 * by name), then spawns a root entity with one child entity per aiNode
 * (Transform from the node matrix, a Mesh component per referenced mesh),
 * preserving the node hierarchy.
 *
 * @param path Path to the model file to import.
 * @param resources Resource manager the imported meshes/materials are added to.
 * @param scene Scene the entity hierarchy is spawned into.
 * @return The root EntityId, or an invalid id on failure.
 */
EntityId importModelIntoScene(
    const std::string& path,
    ResourceManager& resources,
    Scene& scene
);

} // namespace Engine
