#pragma once

#include <string>

/**
 * @brief The names a model file's parts are imported under: the file's project
 *        reference, then the part.
 *
 * Not the stem: `crate.glb` beside `crate.fbx` are two models, and the by-name dedup would give
 * the second the first's parts. A format: scenes store them (docs/reference/resources.md).
 */
namespace Vkm::Engine::ModelPartNames {

/**
 * @brief The name of a model file's mesh.
 *
 * @param ref Project-relative reference to the model file.
 * @param idx The mesh's index in the file's own order.
 * @return `<ref>:mesh<idx>`.
 */
inline std::string meshName(const std::string& ref, int idx) {
    return ref + ":mesh" + std::to_string(idx);
}

/**
 * @brief The name of a model file's material.
 *
 * @param ref Project-relative reference to the model file.
 * @param idx The material's index in the file's own order.
 * @return `<ref>:mat<idx>`.
 */
inline std::string materialName(const std::string& ref, int idx) {
    return ref + ":mat" + std::to_string(idx);
}

/**
 * @brief The name of a model file's rig; a file has one.
 *
 * @param ref Project-relative reference to the model file.
 * @return `<ref>:skeleton`.
 */
inline std::string skeletonName(const std::string& ref) {
    return ref + ":skeleton";
}

/**
 * @brief The name of a model file's animation clip.
 *
 * @param ref Project-relative reference to the model file.
 * @param idx The clip's index in the file's own order.
 * @return `<ref>:clip<idx>`.
 */
inline std::string clipName(const std::string& ref, int idx) {
    return ref + ":clip" + std::to_string(idx);
}

/**
 * @brief The name of an image a model file carries inside it.
 *
 * @param ref Project-relative reference to the model file.
 * @param key The image's reference within the file, with its usage suffix.
 * @return `<ref>:emb:<key>`.
 */
inline std::string embeddedName(const std::string& ref, const std::string& key) {
    return ref + ":emb:" + key;
}

} // namespace Vkm::Engine::ModelPartNames
