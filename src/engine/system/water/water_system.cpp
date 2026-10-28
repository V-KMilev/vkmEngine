#include "system/water/water_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "core/math/bounds.h"
#include "debug/profiler.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/buoyancy.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/physics_settings.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "system/physics/body_pose.h"
#include "system/physics/collision/part_bounds.h"

namespace Vkm::Engine {

namespace {

// An axis this much longer than the thinnest still counts as thin, so a near-cube rolls to any face.
constexpr float FLAT_SLACK = 1.1f;

// The least a box is taken to reach below its middle, metres, so a flat one's share under stays finite.
constexpr float MIN_REACH = 0.01f;

// A triangle seen this edge-on from above lies on no cell: it would stand the water up a wall.
constexpr float LEAST_SPAN = 1e-6f;

// How near critical a float's bob is damped: at half, one landing settles in a bob or two.
constexpr float HEAVE_DAMPING = 0.5f;

} // namespace

void WaterSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("WaterSystem");
    build(ctx);
}

void WaterSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("WaterSystem::fixed");
    build(ctx);
    floatBodies(ctx, ctx.clock.getFixedStep());
}

void WaterSystem::build(FrameContext& ctx) {
    ctx.water = &m_water;
    m_water.m_ocean = ctx.ocean;

    Scene&                 scene     = ctx.scene;
    const ResourceManager& resources = ctx.resources;
    m_kept.clear();
    m_water.m_seaSpans.clear();

    // The materials that are water first, so a world with none walks no mesh at all.
    m_waters.clear();
    resources.forEachOfType<MaterialAsset>([&](MaterialHandle handle, const MaterialAsset& material) {
        if (material.water != WaterKind::None) m_waters.push_back(handle);
    });
    if (m_waters.empty()) {
        m_water.m_bodies.clear();
        return;
    }
    scene.forEach<Mesh, Transform>([&](EntityId id, const Mesh& mesh, const Transform& transform) {
        if (std::find(m_waters.begin(), m_waters.end(), mesh.material) == m_waters.end()) return;
        const MaterialAsset* material = resources.tryGet(mesh.material);
        const MeshAsset*     geometry = resources.tryGet(mesh.mesh);
        if (!geometry) return;
        if (material->water == WaterKind::Sea) {
            const glm::mat4  placed  = resolvedWorldMatrix(scene, id, transform);
            const Math::AABB covered = Math::transform(placed, geometry->bounds());
            m_water.m_seaSpans.emplace_back(covered.min.x, covered.min.z, covered.max.x, covered.max.z);
            return;
        }

        const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);
        // The grid laid last time, while nothing it was laid from has changed.
        const auto is  = [&](const WaterBody& b) { return b.entity == id; };
        const auto was = std::find_if(m_water.m_bodies.begin(), m_water.m_bodies.end(), is);
        if (was != m_water.m_bodies.end() && was->model == model && was->meshUid == geometry->uid()
            && was->meshVersion == geometry->version() && was->materialUid == material->uid()
            && was->materialVersion == material->version()) {
            m_kept.push_back(std::move(*was));
            return;
        }
        WaterBody body;
        body.entity          = id;
        body.model           = model;
        body.meshUid         = geometry->uid();
        body.meshVersion     = geometry->version();
        body.materialUid     = material->uid();
        body.materialVersion = material->version();
        layOut(body, *geometry, *material, model);
        m_kept.push_back(std::move(body));
    });
    m_water.m_bodies.swap(m_kept);
}

void WaterSystem::layOut(
    WaterBody& body,
    const MeshAsset& mesh,
    const MaterialAsset& material,
    const glm::mat4& model
) {
    // The surface in the world: each corner's place and the way its +U runs.
    std::vector<glm::vec3> places(mesh.vertices.size());
    std::vector<glm::vec3> runs(mesh.vertices.size());
    glm::vec2 least(INFINITY);
    glm::vec2 most(-INFINITY);
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        places[v] = glm::vec3(model * glm::vec4(mesh.vertices[v].position, 1.0f));
        runs[v]   = glm::vec3(model * glm::vec4(glm::vec3(mesh.vertices[v].tangent), 0.0f));
        least     = glm::min(least, glm::vec2(places[v].x, places[v].z));
        most      = glm::max(most, glm::vec2(places[v].x, places[v].z));
    }
    body.cells.clear();
    body.columns = 0;
    body.rows    = 0;
    body.depth   = std::max(material.waterDepth, 0.0f);
    if (places.empty()) return;

    const glm::vec2 extent = most - least;
    body.cell    = std::max(LEAST_CELL, std::max(extent.x, extent.y) / float(MOST_CELLS));
    body.corner  = least;
    body.columns = uint32_t(std::ceil(extent.x / body.cell)) + 1;
    body.rows    = uint32_t(std::ceil(extent.y / body.cell)) + 1;
    body.cells.assign(size_t(body.columns) * body.rows, WaterCell{});

    // Each triangle onto the cells whose middles it lies over; where two do, the higher is the water.
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t ia = mesh.indices[t];
        const uint32_t ib = mesh.indices[t + 1];
        const uint32_t ic = mesh.indices[t + 2];
        const glm::vec3& a = places[ia];
        const glm::vec3& b = places[ib];
        const glm::vec3& c = places[ic];
        const float span = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::abs(span) < LEAST_SPAN) continue;
        glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        if (normal.y < 0.0f) normal = -normal;

        const float lowX = std::min({a.x, b.x, c.x});
        const float lowZ = std::min({a.z, b.z, c.z});
        const float topX = std::max({a.x, b.x, c.x});
        const float topZ = std::max({a.z, b.z, c.z});
        // The cells whose middles lie between @p low and @p top along an axis whose grid starts at @p from.
        const auto  first = [&](float low, float from) {
            return std::max(int(std::ceil((low - from) / body.cell - 0.5f)), 0);
        };
        const auto  last  = [&](float top, float from, uint32_t count) {
            return std::min(int(std::floor((top - from) / body.cell - 0.5f)), int(count) - 1);
        };
        for (int j = first(lowZ, body.corner.y); j <= last(topZ, body.corner.y, body.rows); ++j) {
            for (int i = first(lowX, body.corner.x); i <= last(topX, body.corner.x, body.columns); ++i) {
                const float x = body.corner.x + (float(i) + 0.5f) * body.cell;
                const float z = body.corner.y + (float(j) + 0.5f) * body.cell;
                // The middle's weights in the triangle, read on the XZ plane.
                const float wb = ((x - a.x) * (c.z - a.z) - (c.x - a.x) * (z - a.z)) / span;
                const float wc = ((b.x - a.x) * (z - a.z) - (x - a.x) * (b.z - a.z)) / span;
                const float wa = 1.0f - wb - wc;
                if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
                const float height = wa * a.y + wb * b.y + wc * c.y;
                WaterCell& cell = body.cells[size_t(j) * body.columns + size_t(i)];
                if (cell.wet && cell.height >= height) continue;
                const glm::vec3 run = wa * runs[ia] + wb * runs[ib] + wc * runs[ic];
                const float     along = glm::length(run);
                cell.wet    = true;
                cell.height = height;
                cell.normal = normal;
                cell.flow   = along > 0.0f ? run / along * material.flowSpeed : glm::vec3(0.0f);
            }
        }
    }
}

void WaterSystem::floatBodies(FrameContext& ctx, float dt) {
    Scene& scene = ctx.scene;
    const float gravity = glm::length(scene.physics().gravity);
    scene.forEach<Buoyancy, Rigidbody, Transform, Collider>(
        [&](
            EntityId id,
            const Buoyancy& buoyant,
            Rigidbody& body,
            const Transform& transform,
            const Collider& shape
        ) {
            if (body.motion != RigidbodyMotion::Dynamic || !ctx.net.simulates(scene, id)) return;

            // The body as its collider's box, in the world.
            const BodyPose   pose  = worldPoseOf(scene, id, transform);
            const Math::AABB box   = colliderLocalBounds(shape);
            const glm::vec3  half  = 0.5f * (box.max - box.min);
            const glm::vec3  at    = pose.position + pose.rotation * (0.5f * (box.min + box.max));
            const WaterSample water = m_water.at(at);
            if (!water.found) return;

            // How far the box reaches below its middle, however it lies; and which way it floats: on a
            // face of its thinnest axis - a raft lies flat - and of those, the one nearest up.
            const glm::mat3 axes  = glm::mat3_cast(pose.rotation);
            const float     thin  = glm::min(half.x, glm::min(half.y, half.z));
            float           reach = 0.0f;
            int             upmost = -1;
            for (int i = 0; i < 3; ++i) {
                reach += glm::abs(axes[i].y) * half[i];
                const bool flat = half[i] <= thin * FLAT_SLACK;
                if (flat && (upmost < 0 || glm::abs(axes[i].y) > glm::abs(axes[upmost].y))) upmost = i;
            }
            reach = glm::max(reach, MIN_REACH);
            const float under = glm::clamp((water.surface - (at.y - reach)) / (2.0f * reach), 0.0f, 1.0f);
            if (under <= 0.0f) return;

            // Slowed toward the water's own speed, its bob damped at HEAVE_DAMPING of critical - the
            // lift is a spring of gB/2r a metre under - then lifted at its middle, so it never tips
            // over itself, and turned so its face nearest up follows the surface.
            const glm::vec3 face  = axes[upmost] * glm::sign(axes[upmost].y);
            const glm::vec3 turn  = glm::cross(face, water.normal) * (buoyant.righting * under);
            const float     keep  = std::exp(-buoyant.drag * under * dt);
            // Damped wholly from as deep as it rests (1/buoyancy under), less while barely touching.
            const float     bob   = std::sqrt(gravity * buoyant.buoyancy / (2.0f * reach));
            const float     wet   = glm::min(under * buoyant.buoyancy, 1.0f);
            const float     still = std::exp(-2.0f * HEAVE_DAMPING * bob * wet * dt);
            glm::vec3 moving = water.flow + (body.linearVelocity - water.flow) * keep;
            moving.y         = water.flow.y + (moving.y - water.flow.y) * still;
            moving.y        += gravity * buoyant.buoyancy * under * dt;
            body.linearVelocity  = moving;
            body.angularVelocity = (body.angularVelocity + turn * dt) * keep;
            Rigidbody::wake(body);
        }
    );
}

} // namespace Vkm::Engine
