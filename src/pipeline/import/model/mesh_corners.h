#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "resource/asset/mesh_asset.h"

// The mesh builder the model readers share: corners in, a welded mesh out, and its skin's weights.
namespace Vkm::Engine::ModelReaders {

// A mesh's log label: its index in the file, which also names its asset.
std::string meshLabel(const std::string& ref, size_t index);

// One influence on one vertex, before the four that survive are quantized.
struct VertexInfluence {
    uint16_t bone   = 0;
    float    weight = 0.0f;
};

/**
 * @brief Quantize one vertex's influences into the four bytes a SkinVertex holds.
 *
 * The strongest four, the lower bone first of two alike, are renormalized among themselves, then
 * rounded with the error on the first, so the bytes sum to exactly 255 and no vertex stage
 * renormalizes.
 *
 * @param influences The vertex's influences, any order, weights not normalized;
 *        sorted and truncated in place.
 * @return The quantized binding, or all-zero weights when nothing influences
 *         the vertex.
 */
SkinVertex quantizeInfluences(std::vector<VertexInfluence>& influences);

/**
 * @brief A mesh as three corners per triangle, before tangents and welding.
 *
 * MikkTSpace wants per-corner tangents and a weld after, so disagreeing tangents split a vertex.
 */
struct Corners {
    std::vector<Vertex>     vertices;
    std::vector<SkinVertex> skin;                 ///< Empty, or parallel to `vertices`.
    bool                    hasUVs      = false;
    bool                    hasTangents = false;  ///< The file authored them.
};

/**
 * @brief Give @p corners their tangents and weld them into an indexed mesh.
 *
 * Byte-equal corners (vertex and skin) become one vertex, in first-appearance order, so the
 * index buffer keeps the file's triangle order.
 *
 * @param corners The triangle list; consumed.
 * @param mesh Name the log lines report against.
 * @return The welded geometry, with no bounds, rig or recipe yet.
 */
MeshAsset weldCorners(Corners corners, const std::string& mesh);

// Counts of what a skin import refused or repaired, reported once per mesh.
struct SkinReport {
    unsigned outOfRange = 0;  ///< Influences naming a joint the skin does not have.
    unsigned unweighted = 0;  ///< Vertices nothing influences.
};

/// Log what @p report counted for @p mesh, once.
void logSkinReport(const SkinReport& report, const std::string& mesh);

} // namespace Vkm::Engine::ModelReaders
