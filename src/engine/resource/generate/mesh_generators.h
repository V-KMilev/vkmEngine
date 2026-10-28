#pragma once

#include <nlohmann/json_fwd.hpp>

#include "resource/asset/mesh_asset.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Put a generated mesh into @p resources, reusing the one already there
 *        under its name.
 *
 * The name comes from the stamped descriptor ("mesh:generator:sphere:16"),
 * so identical calls share one asset. A plain add() would replace it: a fresh
 * uid dropping in-flight decodes and a needless re-upload. Nothing edits a
 * generated mesh; a variant is different parameters, so a different name.
 * Where a host installed a mesh import (recipeImport), that import builds it
 * from its recipe.
 *
 * @param resources Asset graph to look the name up in and add to.
 * @param mesh A mesh straight from one of the generators below, descriptor and all.
 * @return Handle to the mesh under that name, existing or newly added.
 */
MeshHandle addGeneratedMesh(ResourceManager& resources, MeshAsset mesh);

/**
 * @brief Rebuild a mesh from the recipe one of the generators below stamped.
 *
 * A parameter the recipe lacks reads as the generator's default. Added unnamed:
 * the library's name need not be the one addGeneratedMesh would spell.
 *
 * @param source A `generator` source descriptor.
 * @param resources Asset graph to add the mesh to.
 * @return The mesh, or an invalid handle for a type no generator here has.
 */
MeshHandle createGeneratedMesh(const nlohmann::json& source, ResourceManager& resources);

/**
 * @brief Generate a triangle mesh.
 * @param size Uniform scale of the unit triangle.
 * @return The triangle.
 */
MeshAsset generateTriangle(float size = 1.0f);

/**
 * @brief Generate a plane mesh (quad), tessellated into a segment grid.
 * @param width Full width along x.
 * @param depth Full depth along z.
 * @param widthSegments Quads along width.
 * @param depthSegments Quads along depth.
 * @return The plane.
 */
MeshAsset generatePlane(
    float width = 1.0f,
    float depth = 1.0f,
    uint32_t widthSegments = 1,
    uint32_t depthSegments = 1
);

/**
 * @brief Generate a unit cube mesh (1 unit per side, spanning -0.5 to +0.5 on all axes).
 * @return The cube.
 */
MeshAsset generateCube();

/**
 * @brief Generate a unit sphere mesh (radius 0.5), built as a cube-sphere.
 *
 * No poles, so no pinching or degenerate tangents.
 *
 * @param resolution Quads along each edge of every cube face; clamped up to 2.
 * @return The sphere.
 */
MeshAsset generateSphere(uint32_t resolution = 16);

/**
 * @brief Generate a pyramid mesh (square base on y=0, apex at +height).
 * @param baseSize Edge length of the square base.
 * @param height Apex height above the base.
 * @return The pyramid.
 */
MeshAsset generatePyramid(float baseSize = 1.0f, float height = 1.0f);

/**
 * @brief Generate a cone standing on the Y axis, centred on the origin.
 *
 * The slope normal comes from the slope, not its gradient, so a zero-height cone
 * (a recipe can say anything) is an upward disc rather than NaN.
 *
 * @param radius   Base radius.
 * @param height   Total height; base and apex sit at +/- height/2.
 * @param segments Around the base circle; clamped up to 3.
 * @return The cone.
 */
MeshAsset generateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 16);

/**
 * @brief Generate a capped cylinder standing on the Y axis, centred on the origin.
 *
 * The caps have their own vertex ring so the rim stays a hard edge. The wall
 * duplicates its first column at u = 1, or the seam would interpolate the whole
 * texture backwards across one quad.
 *
 * @param radius   Radius of the wall and both caps.
 * @param height   Total height; the ends sit at +/- height/2.
 * @param segments Around the circumference; clamped up to 3.
 * @return The cylinder.
 */
MeshAsset generateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 20);

/**
 * @brief Generate a flat ring lying on the XZ plane, facing +Y, centred on the origin.
 *
 * A marker on the ground, a halo, a target: one sided, so its material is what
 * decides whether it shows from below. U runs round it, V from the inner edge
 * (0) to the outer (1).
 *
 * @param innerRadius Radius of the hole; zero makes a disc.
 * @param outerRadius Radius of the outer edge.
 * @param segments    Around the circumference; clamped up to 3.
 * @return The ring.
 */
MeshAsset generateRing(float innerRadius = 0.25f, float outerRadius = 0.5f, uint32_t segments = 32);

} // namespace Vkm::Engine
