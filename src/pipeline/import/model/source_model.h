#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/core/transform.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief One node of a model file's hierarchy.
 */
struct SourceNode {
    std::string           name;
    int32_t               parent = -1;  ///< Index into SourceModel::nodes; always lower than this node's.
    Transform             local;
    std::vector<uint32_t> meshes;       ///< Indices into SourceModel::meshes.
    std::vector<uint32_t> children;     ///< Indices into SourceModel::nodes, in file order.
};

/**
 * @brief A joint one mesh's skin names, and how that mesh sat against it at bind.
 */
struct SourceBone {
    uint32_t  node = 0;           ///< Index into SourceModel::nodes.
    glm::mat4 inverseBind{1.0f};  ///< Mesh space to the joint's space, at bind.
};

/**
 * @brief One drawable piece of a model file: a single material's triangles.
 *
 * Welded, with tangents, bound to no rig: skin indices address `bones`; all-zero weights mean
 * no influence. The rig is decided across every mesh after the parse.
 */
struct SourceMesh {
    MeshAsset               geometry;
    std::vector<SourceBone> bones;
    uint32_t                material = 0;  ///< Index into SourceModel::materials.
};

/**
 * @brief An image a material samples: encoded bytes the file carries, or a path beside it.
 */
struct SourceImage {
    std::string                ref;       ///< "*<index>" when embedded, else the path as written.
    std::vector<unsigned char> embedded;  ///< The encoded file (PNG, JPEG, ...) when embedded.
};

/**
 * @brief One texture slot of a material, bound to one of the file's images.
 */
struct SourceMap {
    TextureHandle MaterialAsset::* slot = nullptr;
    TextureUsage                   usage = TextureUsage::Data;
    uint32_t                       image = 0;  ///< Index into SourceModel::images.
};

/**
 * @brief A material as the file describes it, its maps not yet loaded.
 */
struct SourceMaterial {
    MaterialAsset          values;  ///< Scalars only; no texture handle set.
    std::vector<SourceMap> maps;
};

/**
 * @brief One node's keys in one clip, in seconds from the clip's start.
 */
struct SourceChannel {
    uint32_t               node = 0;  ///< Index into SourceModel::nodes.
    std::vector<float>     positionTimes;
    std::vector<glm::vec3> positions;
    std::vector<float>     rotationTimes;
    std::vector<glm::quat> rotations;
    std::vector<float>     scaleTimes;
    std::vector<glm::vec3> scales;
};

/**
 * @brief One animation in a model file, keyed for a linear sampler.
 */
struct SourceClip {
    float                      duration = 0.0f;
    std::vector<SourceChannel> channels;
};

/**
 * @brief What a model file holds, read once into the engine's own terms.
 *
 * Everything past the parse is built from this, once for every format. `nodes[0]` is the file's
 * root, which the import's root entity stands for.
 */
struct SourceModel {
    std::vector<SourceNode>     nodes;
    std::vector<SourceMesh>     meshes;
    std::vector<SourceMaterial> materials;
    std::vector<SourceImage>    images;
    std::vector<SourceClip>     clips;
};

/**
 * @brief Parse a model file into the engine's terms.
 *
 * glTF/GLB through cgltf, FBX and OBJ through ufbx. The result is right-handed, +Y up, metres, V
 * up, welded, with tangents (the file's or MikkTSpace's). List order is the file's and names
 * assets, so it is part of the format (docs/reference/resources.md).
 *
 * @param path The model file on this machine.
 * @param ref Its project reference, which the log lines name.
 * @return The parsed file, or null when it cannot be read (logged).
 */
std::unique_ptr<SourceModel> parseModel(const std::string& path, const std::string& ref);

/**
 * @brief parseModel, once for each version of a file's bytes.
 *
 * Otherwise a glTF with N meshes, asked for asset by asset, is parsed N times. A few recent files
 * are kept, reused while every file the parse read (AssetCooker::sourceFilesAt) keeps its size and
 * write time. A request waits for another worker's parse of the same file; a failure is not kept.
 * Callable from a worker, as @p path arrives resolved.
 *
 * @param path The model file on this machine.
 * @param ref Its project reference, which the log lines name.
 * @return The parsed file, shared, or null when it cannot be read (logged).
 */
std::shared_ptr<const SourceModel> loadSourceModel(const std::string& path, const std::string& ref);

} // namespace Vkm::Engine
