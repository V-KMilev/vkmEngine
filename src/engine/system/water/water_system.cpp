#include "system/water/water_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "base/math/aabb.h"
#include "base/profile/profiler.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/physics/buoyancy.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/landscape/water.h"
#include "ecs/settings/physics_settings.h"
#include "ecs/scene.h"
#include "ecs/water/swell.h"
#include "net/net_session.h"
#include "vkm_platform/threading/thread_pool.h"
#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "core/frame/body_pose.h"

namespace Vkm::Engine {

namespace {

// An axis this much longer than the thinnest still counts as thin, so a near-cube rolls to any face.
constexpr float FLAT_SLACK = 1.1f;

// The least a box is taken to reach below its middle, meters, so a flat one's share under stays finite.
constexpr float MIN_REACH = 0.01f;

// A triangle seen this edge-on from above lies on no cell: it would stand the water up a wall.
constexpr float LEAST_SPAN = 1e-6f;

// How near critical a float's bob is damped: at half, one landing settles in a bob or two.
constexpr float HEAVE_DAMPING = 0.5f;

// How far apart @p mesh's vertices, placed at @p places, lie across the XZ plane: the leg of the right
// triangle as large as its triangles' mean there. A grid finer than a mesh's triangles says nothing
// more of a surface flat within each.
float spacingOf(const MeshAsset& mesh, const std::vector<glm::vec3>& places) {
    double area = 0.0;
    size_t triangles = 0;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const glm::vec3& a = places[mesh.indices[t]];
        const glm::vec3& b = places[mesh.indices[t + 1]];
        const glm::vec3& c = places[mesh.indices[t + 2]];
        area += 0.5 * std::abs(double((b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z)));
        ++triangles;
    }
    return triangles > 0 ? float(std::sqrt(2.0 * area / double(triangles))) : 0.0f;
}

} // namespace

void WaterSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("WaterSystem");
    build(ctx);
}

void WaterSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("WaterSystem::fixed");
    build(ctx);
    if (!ctx.scene.water().bodies().empty() && floatBodies(ctx, ctx.clock.getFixedStep())) drawAhead(ctx);
}

void WaterSystem::build(FrameContext& ctx) {
    const Scene& scene = ctx.scene;
    WaterData& built = ctx.scene.water();
    const ResourceManager& resources = ctx.resources;
    m_kept.clear();
    bool swelling = false;
    scene.forEach<Water, Mesh, Transform>(
        [&](EntityId id, const Water& water, const Mesh& mesh, const Transform& transform) {
            const MeshAsset* geometry = resources.tryGet(mesh.mesh);
            if (!geometry) return;
            swelling = swelling || water.waves > 0.0f;
            const glm::mat4 model = resolvedWorldMatrix(scene, id, transform);

            // The grid laid last time, while nothing it was laid from has changed; moved, it moves along.
            const auto is  = [&](const WaterBody& b) { return b.entity == id; };
            const auto was = std::find_if(built.m_bodies.begin(), built.m_bodies.end(), is);
            if (was != built.m_bodies.end() && was->turn == glm::mat3(model)
                && was->meshUid == geometry->uid() && was->meshVersion == geometry->version()) {
                m_kept.push_back(std::move(*was));
                m_kept.back().water  = water;
                m_kept.back().origin = glm::vec3(model[3]);
                return;
            }
            WaterBody body;
            body.entity      = id;
            body.water       = water;
            body.meshUid     = geometry->uid();
            body.meshVersion = geometry->version();
            layOut(body, *geometry, model);
            m_kept.push_back(std::move(body));
        }
    );
    built.m_bodies.swap(m_kept);
    if (built.m_bodies.empty()) {
        built.m_waves = WaveFrame{};
        return;
    }
    buildWaves(ctx, swelling);
}

void WaterSystem::buildWaves(FrameContext& ctx, bool swelling) {
    const WaveSettings& waves = ctx.scene.environment().waves;
    const double seconds = worldTime(ctx.net, ctx.clock);
    if (swelling && m_shaped != waves.shape) {
        m_swell  = Swell::of(waves.shape);
        m_shaped = waves.shape;
    }
    WaveFrame& frame = ctx.scene.water().m_waves;
    frame.surface = WaveSurface::at(swelling ? m_swell : nullptr, waves, seconds);
    const SwellMoment* moment = frame.surface.swell.get();
    if (moment && m_ahead && m_ahead->cycle() == moment->cycle() && &m_ahead->swell() == m_swell.get()) {
        frame.surface.swell = m_ahead;
    }
    frame.ripples = Ripples::at(waves, seconds);

    // The maps drift downwind at the speed of a wave as long as one repeat, wrapped in double
    // so they stay smooth however long the water has run.
    const double heading = glm::radians(double(waves.shape.windDirection));
    const double scale   = double(WaveSettings::DETAIL_SCALE);
    const double speed   = std::sqrt(WaveSpectrum::GRAVITY * scale / glm::two_pi<double>());
    const double moved   = speed * seconds / scale;
    const auto wrap = [](double repeats) { return float(repeats - std::floor(repeats)); };
    frame.detailDrift = glm::vec2(wrap(std::sin(heading) * moved), wrap(std::cos(heading) * moved));
}

void WaterSystem::layOut(WaterBody& body, const MeshAsset& mesh, const glm::mat4& model) {
    // The surface turned into the world about its origin: each corner's place and the way its +U runs.
    std::vector<glm::vec3> places(mesh.vertices.size());
    std::vector<glm::vec3> runs(mesh.vertices.size());
    glm::vec2 least(INFINITY);
    glm::vec2 most(-INFINITY);
    const glm::mat3 turn(model);
    body.turn   = turn;
    body.origin = glm::vec3(model[3]);
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        places[v] = turn * mesh.vertices[v].position;
        runs[v]   = turn * glm::vec3(mesh.vertices[v].tangent);
        least     = glm::min(least, glm::vec2(places[v].x, places[v].z));
        most      = glm::max(most, glm::vec2(places[v].x, places[v].z));
    }
    body.cells.clear();
    body.columns = 0;
    body.rows    = 0;
    if (places.empty()) return;

    const glm::vec2 extent = most - least;
    const float     widest = std::max(extent.x, extent.y) / float(MOST_CELLS);
    body.cell    = std::max({LEAST_CELL, spacingOf(mesh, places), widest});
    body.corner  = least;
    body.columns = uint32_t(std::ceil(extent.x / body.cell)) + 1;
    body.rows    = uint32_t(std::ceil(extent.y / body.cell)) + 1;
    body.cells.assign(size_t(body.columns) * body.rows, WaterCell{});

    // Each triangle onto the cells whose middles it lies over; where two do, the higher is the water.
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t ia  = mesh.indices[t];
        const uint32_t ib  = mesh.indices[t + 1];
        const uint32_t ic  = mesh.indices[t + 2];
        const glm::vec3& a = places[ia];
        const glm::vec3& b = places[ib];
        const glm::vec3& c = places[ic];
        const float span   = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
        if (std::abs(span) < LEAST_SPAN) continue;

        // The cells whose middles lie between @p low and @p top along an axis whose grid starts at @p from,
        // which neither passes: a cell's middle at i + 0.5 cells, so the first is the one round @p low
        // when its middle is not before it, and the last the one round @p top when its middle is not
        // past it.
        const float scale = 1.0f / body.cell;
        const auto first = [&](float low, float from) {
            const float at   = (low - from) * scale;
            const int   cell = int(at);
            return at - float(cell) <= 0.5f ? cell : cell + 1;
        };
        const auto last = [&](float top, float from, uint32_t count) {
            const float at   = (top - from) * scale;
            const int   cell = int(at);
            return std::min(at - float(cell) >= 0.5f ? cell : cell - 1, int(count) - 1);
        };
        const int i0 = first(std::min({a.x, b.x, c.x}), body.corner.x);
        const int i1 = last(std::max({a.x, b.x, c.x}), body.corner.x, body.columns);
        const int j0 = first(std::min({a.z, b.z, c.z}), body.corner.y);
        const int j1 = last(std::max({a.z, b.z, c.z}), body.corner.y, body.rows);
        if (i0 > i1 || j0 > j1) continue;
        glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        if (normal.y < 0.0f) normal = -normal;

        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float x = body.corner.x + (float(i) + 0.5f) * body.cell;
                const float z = body.corner.y + (float(j) + 0.5f) * body.cell;
                // The middle's weights in the triangle, read on the XZ plane.
                const float wb = ((x - a.x) * (c.z - a.z) - (c.x - a.x) * (z - a.z)) / span;
                const float wc = ((b.x - a.x) * (z - a.z) - (x - a.x) * (b.z - a.z)) / span;
                const float wa = 1.0f - wb - wc;
                if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
                const float height = wa * a.y + wb * b.y + wc * c.y;
                WaterCell& cell    = body.cells[size_t(j) * body.columns + size_t(i)];
                if (cell.wet && cell.height >= height) continue;
                const glm::vec3 run    = wa * runs[ia] + wb * runs[ib] + wc * runs[ic];
                const float     length = glm::length(run);
                cell.wet    = true;
                cell.height = height;
                cell.normal = normal;
                cell.along  = length > 0.0f ? run / length : glm::vec3(0.0f);
            }
        }
    }
}

void WaterSystem::drawAhead(const FrameContext& ctx) {
    if (!ctx.scene.water().m_waves.surface.swell) return;
    // Inside a tick the world's clock is that tick's, so the next is a step on.
    const double next = worldTime(ctx.net, ctx.clock) + double(ctx.clock.getFixedStep());
    m_ahead = std::make_shared<const SwellMoment>(m_swell, WaveSurface::cycleOf(next));
    Platform::ThreadPool::get().addTask([ahead = m_ahead] {
        PROFILE_SCOPE("Water/SwellDraw");
        ahead->grid(0);
    });
}

bool WaterSystem::floatBodies(FrameContext& ctx, float dt) {
    Scene& scene = ctx.scene;
    const float gravity = glm::length(scene.physics().gravity);
    bool asked = false;
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
            const BodyPose pose = worldPoseOf(scene, id, transform);
            const Physics::AABB box = colliderLocalBounds(shape);
            const glm::vec3 half = 0.5f * (box.max - box.min);
            const glm::vec3 at   = pose.position + pose.rotation * (0.5f * (box.min + box.max));
            const WaterSample water = scene.water().at(at);
            asked = true;
            if (!water.found) return;

            // How far the box reaches below its middle, however it lies; and which way it floats: on a
            // face of its thinnest axis - a raft lies flat - and of those, the one nearest up.
            const glm::mat3 axes = glm::mat3_cast(pose.rotation);
            const float thin = glm::min(half.x, glm::min(half.y, half.z));
            float reach = 0.0f;
            int upmost = -1;
            for (int i = 0; i < 3; ++i) {
                reach += glm::abs(axes[i].y) * half[i];
                const bool flat = half[i] <= thin * FLAT_SLACK;
                if (flat && (upmost < 0 || glm::abs(axes[i].y) > glm::abs(axes[upmost].y))) upmost = i;
            }
            reach = glm::max(reach, MIN_REACH);
            const float under = glm::clamp((water.surface - (at.y - reach)) / (2.0f * reach), 0.0f, 1.0f);
            if (under <= 0.0f) return;

            // Slowed toward the water's own speed, its bob damped at HEAVE_DAMPING of critical - the
            // lift is a spring of gB/2r a meter under - then lifted at its middle, so it never tips
            // over itself, and turned so its face nearest up follows the surface.
            const glm::vec3 face = axes[upmost] * glm::sign(axes[upmost].y);
            const glm::vec3 turn = glm::cross(face, water.normal) * (buoyant.righting * under);
            const float keep     = std::exp(-buoyant.drag * under * dt);
            // Damped wholly from as deep as it rests (1/buoyancy under), less while barely touching.
            const float bob   = std::sqrt(gravity * buoyant.buoyancy / (2.0f * reach));
            const float wet   = glm::min(under * buoyant.buoyancy, 1.0f);
            const float still = std::exp(-2.0f * HEAVE_DAMPING * bob * wet * dt);
            glm::vec3 moving  = water.flow + (body.linearVelocity - water.flow) * keep;
            moving.y = water.flow.y + (moving.y - water.flow.y) * still;
            moving.y        += gravity * buoyant.buoyancy * under * dt;
            body.linearVelocity  = moving;
            body.angularVelocity = (body.angularVelocity + turn * dt) * keep;
            Rigidbody::wake(body);
        }
    );
    return asked;
}

} // namespace Vkm::Engine
