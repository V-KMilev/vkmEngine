#pragma once

#include <cstdint>

#include "resource/asset/mesh_asset.h"

namespace Engine {

/**
 * @brief Procedural primitives for gameplay code, built without the tools module.
 *
 * The engine's real generators (tools/generator/mesh_generators.h) live in the
 * editor/runtime-side tools target, which `game` deliberately does not link -
 * gameplay depends on EngineCore only, so the same sources compile into both the
 * static runtime library and the editor's hot-reload module. These mirror the
 * generators' winding, normals and tangents exactly, so a procedural prop shades
 * and face-culls identically to a cooked one.
 *
 * Each primitive is a unit shape centred on the origin: scale it with the
 * entity Transform rather than baking size into the mesh, so every instance of
 * a shape shares one MeshHandle and the instance batcher can merge them.
 */

/**
 * @brief Build the unit cube: 24 vertices (four per face, so each face carries
 *        its own normal and tangent) and 12 triangles.
 *
 * @return A cube spanning [-0.5, 0.5] on every axis.
 */
MeshAsset makeCubeMesh();

/**
 * @brief Build a UV sphere of unit diameter.
 *
 * Segment counts drive the triangle budget directly ((x * y) * 2 triangles),
 * which is the knob to turn when the point of a scene is vertex-stage load.
 *
 * @param xSegments Longitudinal divisions (around the Y axis).
 * @param ySegments Latitudinal divisions (pole to pole).
 * @return A sphere of radius 0.5 centred on the origin.
 */
MeshAsset makeSphereMesh(uint32_t xSegments = 24, uint32_t ySegments = 12);

/**
 * @brief Build a closed cylinder of unit height and unit diameter.
 *
 * Side vertices are duplicated at the UV seam and the caps carry their own
 * ring, so normals stay hard at the rim instead of smearing across it.
 *
 * @param segments Radial divisions around the Y axis.
 * @return A cylinder of radius 0.5 spanning [-0.5, 0.5] on Y.
 */
MeshAsset makeCylinderMesh(uint32_t segments = 20);

} // namespace Engine
