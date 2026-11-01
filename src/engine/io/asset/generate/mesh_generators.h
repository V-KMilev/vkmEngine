#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

#include "base/reflect/reflect.h"
#include "resource/asset/mesh_asset.h"
#include "resource/handle.h"

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
 * @brief A triangle's shape: its generator's parameters, as its recipe keeps them, and their defaults.
 */
struct TriangleParams {
    float size = 1.0f;  ///< Uniform scale of the unit triangle.
};

/**
 * @brief A plane's shape, as TriangleParams.
 */
struct PlaneParams {
    float    width         = 1.0f;  ///< Full width along x.
    float    depth         = 1.0f;  ///< Full depth along z.
    uint32_t widthSegments = 1;     ///< Quads along width.
    uint32_t depthSegments = 1;     ///< Quads along depth.
};

/**
 * @brief A sphere's shape, as TriangleParams.
 */
struct SphereParams {
    uint32_t resolution = 16;  ///< Quads along each edge of every cube face; clamped up to 2.
};

/**
 * @brief A pyramid's shape, as TriangleParams.
 */
struct PyramidParams {
    float baseSize = 1.0f;  ///< Edge length of the square base.
    float height   = 1.0f;  ///< Apex height above the base.
};

/**
 * @brief A cone's shape, as TriangleParams.
 */
struct ConeParams {
    float    radius   = 0.5f;  ///< Base radius.
    float    height   = 1.0f;  ///< Total height; base and apex sit at +/- height/2.
    uint32_t segments = 16;    ///< Around the base circle; clamped up to 3.
};

/**
 * @brief A cylinder's shape, as TriangleParams.
 */
struct CylinderParams {
    float    radius   = 0.5f;  ///< Radius of the wall and both caps.
    float    height   = 1.0f;  ///< Total height; the ends sit at +/- height/2.
    uint32_t segments = 20;    ///< Around the circumference; clamped up to 3.
};

/**
 * @brief A ring's shape, as TriangleParams.
 */
struct RingParams {
    float    innerRadius = 0.25f;  ///< Radius of the hole; zero makes a disc. Below outerRadius.
    float    outerRadius = 0.5f;   ///< Radius of the outer edge.
    uint32_t segments    = 32;     ///< Around the circumference; clamped up to 3.
};

/**
 * @brief Generate a triangle mesh.
 * @param params Its shape.
 * @return The triangle.
 */
MeshAsset generateTriangle(TriangleParams params = {});

/**
 * @brief Generate a plane mesh (quad), tessellated into a segment grid.
 * @param params Its size and grid.
 * @return The plane.
 */
MeshAsset generatePlane(PlaneParams params = {});

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
 * @param params How finely it is cut.
 * @return The sphere.
 */
MeshAsset generateSphere(SphereParams params = {});

/**
 * @brief Generate a pyramid mesh (square base on y=0, apex at +height).
 * @param params Its base and height.
 * @return The pyramid.
 */
MeshAsset generatePyramid(PyramidParams params = {});

/**
 * @brief Generate a cone standing on the Y axis, centered on the origin.
 *
 * The slope normal comes from the slope, not its gradient, so a zero-height cone
 * (a recipe can say anything) is an upward disc rather than NaN.
 *
 * @param params Its radius, height and segments.
 * @return The cone.
 */
MeshAsset generateCone(ConeParams params = {});

/**
 * @brief Generate a capped cylinder standing on the Y axis, centered on the origin.
 *
 * The caps have their own vertex ring so the rim stays a hard edge. The wall
 * duplicates its first column at u = 1, or the seam would interpolate the whole
 * texture backwards across one quad.
 *
 * @param params Its radius, height and segments.
 * @return The cylinder.
 */
MeshAsset generateCylinder(CylinderParams params = {});

/**
 * @brief Generate a flat ring lying on the XZ plane, facing +Y, centered on the origin.
 *
 * A marker on the ground, a halo, a target: one sided, so its material is what
 * decides whether it shows from below. U runs round it, V from the inner edge
 * (0) to the outer (1).
 *
 * @param params Its radii and segments.
 * @return The ring.
 */
MeshAsset generateRing(RingParams params = {});

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::TriangleParams)
    VKM_F(size)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::PlaneParams)
    VKM_F(width)
    VKM_F(depth)
    VKM_F(widthSegments)
    VKM_F(depthSegments)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::SphereParams)
    VKM_F(resolution)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::PyramidParams)
    VKM_F(baseSize)
    VKM_F(height)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::ConeParams)
    VKM_F(radius)
    VKM_F(height)
    VKM_F(segments)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::CylinderParams)
    VKM_F(radius)
    VKM_F(height)
    VKM_F(segments)
VKM_REFLECT_END()

VKM_REFLECT_BEGIN(::Vkm::Engine::RingParams)
    VKM_F(innerRadius)
    VKM_F(outerRadius)
    VKM_F(segments)
VKM_REFLECT_END()
