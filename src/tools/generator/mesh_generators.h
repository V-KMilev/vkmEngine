#pragma once

#include "resource/mesh_asset.h"

namespace Engine {

/**
 * @brief Generate a triangle mesh.
 * @param size Size of the triangle (default: 2.0).
 * @return MeshAsset containing triangle geometry.
 */
MeshAsset generateTriangle(float sides = 2.0f);

/**
 * @brief Generate a plane mesh (quad).
 * @param width Width of the plane (default: 2.0).
 * @param height Height of the plane (default: 2.0).
 * @param widthSegments Number of segments along width (default: 1).
 * @param heightSegments Number of segments along height (default: 1).
 * @return MeshAsset containing plane geometry.
 */
MeshAsset generatePlane(
    float width = 2.0f,
    float height = 2.0f,
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
 * @brief Generate a pyramid mesh (square base).
 * @param baseSize Size of the square base (default: 2.0).
 * @param height Height of the pyramid (default: 2.0).
 * @return MeshAsset containing pyramid geometry.
 */
MeshAsset generatePyramid(float baseSize = 2.0f, float height = 2.0f);

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

} // namespace Engine
