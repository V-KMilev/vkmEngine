#pragma once

#include <string>

#include "resource/mesh_asset.h"
#include "resource/material_asset.h"
#include "resource/resource_handle.h"
#include "ecs/entity.h"   // EntityId is a using-alias (StorageIndex), not forward-declarable

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
 * AssetFactories "model" kind) reproduces the same asset names and the
 * Mesh components re-link by name:
 *   mesh     "<stem>:mesh<index>"
 *   material "<stem>:mat<index>"   (or "<stem>:mat_default")
 * where <stem> is the model file name without extension and the indices
 * are Assimp's global mesh / material indices.
 */

/// Build one aiMesh's geometry. Returns an empty MeshAsset on failure.
/// Sets name + source so the asset round-trips through scene save/load.
MeshAsset loadModelMesh(const std::string& path, int meshIndex);

/// Build + register one material (loading its textures). @p materialIndex
/// < 0 yields a default material. Idempotent by name.
MaterialHandle loadModelMaterial(const std::string& path, int materialIndex,
                                 ResourceManager& resources);

/**
 * @brief Import a whole model file into @p scene.
 *
 * Adds every aiMesh's MeshAsset + MaterialAsset to @p resources (idempotent
 * by name), then spawns a root entity with one child entity per aiNode
 * (Transform from the node matrix, a Mesh component per referenced mesh),
 * preserving the node hierarchy.
 *
 * @return the root EntityId, or an invalid id on failure.
 */
EntityId importModelIntoScene(const std::string& path,
                              ResourceManager& resources, Scene& scene);

} // namespace Engine
