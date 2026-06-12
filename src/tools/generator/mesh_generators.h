#pragma once

#include "resource/asset/mesh_asset.h"

namespace Engine {

/**
 * @brief Generate a triangle mesh.
 * @param size Uniform scale of the unit triangle (default: 1.0).
 * @return MeshAsset containing triangle geometry.
 */
MeshAsset generateTriangle(float size = 1.0f);

/**
 * @brief Generate a plane mesh (quad), tessellated into a segment grid.
 * @param width Full width of the plane along x (default: 1.0).
 * @param height Full depth of the plane along z (default: 1.0).
 * @param widthSegments Number of quads along width (default: 1).
 * @param heightSegments Number of quads along depth (default: 1).
 * @return MeshAsset containing plane geometry.
 */
MeshAsset generatePlane(
    float width = 1.0f,
    float height = 1.0f,
    uint32_t widthSegments = 1,
    uint32_t heightSegments = 1
);

/**
 * @brief Generate a unit cube mesh (from -1 to +1 on all axes).
 * @return MeshAsset containing cube geometry.
 */
MeshAsset generateCube();

/**
 * @brief Generate a sphere mesh.
 * @param xSegments Number of horizontal segments (default: 32).
 * @param ySegments Number of vertical segments (default: 16).
 * @return MeshAsset containing sphere geometry.
 */
MeshAsset generateSphere(uint32_t xSegments = 32, uint32_t ySegments = 16);

/**
 * @brief Generate a pyramid mesh (square base on y=0, apex at +height).
 * @param baseSize Edge length of the square base (default: 1.0).
 * @param height Apex height above the base (default: 1.0).
 * @return MeshAsset containing pyramid geometry.
 */
MeshAsset generatePyramid(float baseSize = 1.0f, float height = 1.0f);

/**
 * @brief Generate a cone mesh.
 * @param radius Radius of the cone base (default: 0.5).
 * @param height Height of the cone (default: 1.0).
 * @param segments Number of segments for the base circle (default: 16).
 * @return MeshAsset containing cone geometry.
 */
MeshAsset generateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 16);

/**
 * @brief Decimate a mesh by vertex clustering.
 *
 * Snaps vertices to a uniform grid over the mesh AABB, averages each occupied
 * cell to one vertex, and drops triangles whose corners collapsed into the same
 * cell. A cheap, general LOD source for arbitrary geometry (where you can't just
 * re-tessellate at a lower resolution). Coarser than edge-collapse / QEM
 * simplification - it can shift the silhouette - but that's invisible at the
 * screen sizes where the coarse LOD levels are selected. Lower @p gridResolution
 * = fewer cells = coarser result. Returns the source unchanged if decimation
 * would collapse the whole mesh or the inputs are degenerate.
 *
 * @param src Source mesh.
 * @param gridResolution Cells per axis across the AABB (e.g. 16 = mild, 6 = aggressive).
 * @return The decimated MeshAsset (bounds recomputed).
 */
MeshAsset decimateMesh(const MeshAsset& src, uint32_t gridResolution);

/**
 * @brief Decimate @p base and stamp a serializable "decimate" source on the result.
 *
 * Same geometry as @ref decimateMesh, but the returned asset carries a
 * `{"kind":"decimate","base":<baseName>,"grid":<n>}` source descriptor and a
 * deterministic name derived from (baseName, grid). That source is what the
 * scene saver emits and what the "decimate" AssetFactory re-runs on load, so a
 * decimated LOD level survives save/load instead of being silently dropped.
 *
 * @param base           Source mesh to cluster.
 * @param baseName       Name of the source mesh (the level the chain decimates from).
 * @param gridResolution Cells per axis across the AABB (see @ref decimateMesh).
 * @return The decimated MeshAsset with source + name stamped.
 */
MeshAsset decimateMeshTracked(const MeshAsset& base, const std::string& baseName, uint32_t gridResolution);

} // namespace Engine
