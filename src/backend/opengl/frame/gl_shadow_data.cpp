#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "frame/gl_shadow_data.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "logger.h"

#include "gl_uniform_buffer.h"

#include "gl_view.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "storage/gl_shadow_atlas.h"
#include "offline/gl_cubemap.h"
#include "gl_buffer_upload.h"
#include "ecs/component/render/light.h"
#include "system/render/render_view.h"
#include "system/render/data/light_data.h"
#include "debug/profiler.h"
#include "platform/threading/thread_pool.h"
#include "core/fnv1a.h"
#include "core/math/frustum.h"

namespace Vkm::Engine {

GLShadowData::GLShadowData() {
    for (int& s : m_lightSlot) s = -1;
}
GLShadowData::~GLShadowData() {
    // The tasks write into this object; none may outlive it.
    finishCull();
}

namespace {

// Range to use for a light that carries no radius.
constexpr float DEFAULT_LIGHT_RANGE = 50.0f;

// ShadowRun::key's high digit: vertex layout, and for a skinned caster whether posed. In draw
// order, so each layout is one stretch of a tile's runs and each program one of a layout's.
constexpr uint32_t STATIC_GROUP  = 0;
constexpr uint32_t SKINNED_GROUP = 1;  ///< Skinned layout, drawn as stored.
constexpr uint32_t POSED_GROUP   = 2;
constexpr uint32_t KEY_GROUPS    = 3;
static_assert(STATIC_GROUP < SKINNED_GROUP && SKINNED_GROUP < POSED_GROUP && POSED_GROUP < KEY_GROUPS);

// World distance the logarithmic cascade split is anchored at, rather than the
// camera's near plane (docs/reference/lighting.md).
constexpr float CASCADE_NEAR = 1.0f;

// The sun's nearest cascades take the largest tile; those past them take half, as each of their
// texels already spans more of the world. Four at full size would fill the atlas.
constexpr uint32_t FULL_CASCADES = 3;
static_assert(FULL_CASCADES < Config::NUM_CASCADES, "Every cascade at full size leaves the spots no room");

// Shadow texels per screen pixel of a spot's cone (Unreal's r.Shadow.TexelsPerPixelSpotlight).
constexpr float SPOT_TEXELS_PER_PIXEL = 1.27324f;

// A spot's tile halves only once its cone needs under this share of it: past half, by a margin.
constexpr float SPOT_SHRINK_BELOW = 0.4f;

// A light's range: its radius, or DEFAULT_LIGHT_RANGE when it carries none.
float lightRange(const LightData& light) {
    return light.radius > 0.0f ? light.radius : DEFAULT_LIGHT_RANGE;
}

uint32_t ceilPowerOfTwo(float v) {
    uint32_t p = 1;
    while (static_cast<float>(p) < v && p < (1u << 30)) p *= 2;
    return p;
}

} // namespace

glm::vec3 stableUp(const glm::vec3& dir) {
    return std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

void GLShadowData::build(const RenderView& view, const GLView& glView, uint32_t tileRes) {
    // A cull still in flight - a frame that threw between the plan and the
    // pass - is writing into the batches about to be reused.
    finishCull();

    m_jobs2D.clear();
    m_jobsCube.clear();
    m_data = ShadowUBOData{};
    for (int& s : m_lightSlot) s = -1;
    m_lightCount = std::min<uint32_t>(static_cast<uint32_t>(view.lights.size()), Config::MAX_LIGHTS);

    // Floored as the atlas floors it, so the last cascade's half tile is a tile.
    m_tileRes = std::max(tileRes, SHADOW_ATLAS_MIN_BLOCK_RES);
    m_tileSizes.clear();
    m_tileOwners.clear();

    m_eye          = view.camera.position;
    m_orthographic = view.camera.projection[3][3] != 0.0f;
    m_focalPixels  = view.camera.projection[1][1] * 0.5f * static_cast<float>(view.viewportHeight);

    // Camera frustum corners in world space, from the inverse view-projection.
    const glm::mat4& invVP = view.camera.invViewProj;
    const glm::vec2 ndc[4] = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
    CameraFrustum cam;
    for (int k = 0; k < 4; ++k) {
        glm::vec4 n = invVP * glm::vec4(ndc[k].x, ndc[k].y, -1.0f, 1.0f);
        glm::vec4 f = invVP * glm::vec4(ndc[k].x, ndc[k].y,  1.0f, 1.0f);
        cam.nearCorners[k] = glm::vec3(n) / n.w;
        cam.farCorners[k]  = glm::vec3(f) / f.w;
    }
    const glm::vec3 camPos = view.camera.position;
    glm::vec3 nearCenter(0.0f), farCenter(0.0f);
    for (int k = 0; k < 4; ++k) { nearCenter += cam.nearCorners[k]; farCenter += cam.farCorners[k]; }
    nearCenter *= 0.25f;
    farCenter  *= 0.25f;

    const glm::vec3 camFwd = glm::normalize(farCenter - nearCenter);
    cam.nearDepth = glm::dot(nearCenter - camPos, camFwd);
    cam.farDepth  = glm::dot(farCenter  - camPos, camFwd);
    if (cam.nearDepth < 0.01f) cam.nearDepth = 0.01f;
    if (cam.farDepth  <= cam.nearDepth) cam.farDepth = cam.nearDepth + 1.0f;

    uint32_t next2D   = 0;
    uint32_t nextCube = 0;
    bool     haveSun  = false;

    // Entity-slot order, not `view.lights` order, which is a SparseSet's packing
    // and moves when an unrelated light is destroyed; the sun first, because its
    // cascades need Config::NUM_CASCADES consecutive tiles.
    m_refused = 0;
    m_shadowOrder.clear();
    // A spot or point whose reach misses the view lights nothing seen: no slot, so one that is
    // seen can have it.
    const Math::Frustum seen = Math::extractFrustum(view.camera.viewProjection);
    for (uint32_t i = 0; i < m_lightCount; ++i) {
        const LightData& light = view.lights[i];
        if (!light.castShadows) continue;
        if (light.type == LightType::Spot || light.type == LightType::Point) {
            const glm::vec3 extent(lightRange(light));
            if (!Math::frustumIntersectsAABB(seen, {light.position - extent, light.position + extent})) {
                continue;
            }
        }
        m_shadowOrder.push_back(i);
    }
    const auto sunFirst = [&](uint32_t i) {
        return view.lights[i].type == LightType::Directional ? 0 : 1;
    };
    std::sort(m_shadowOrder.begin(), m_shadowOrder.end(), [&](uint32_t a, uint32_t b) {
        if (sunFirst(a) != sunFirst(b)) return sunFirst(a) < sunFirst(b);
        return view.lights[a].entitySlot < view.lights[b].entitySlot;
    });

    for (const uint32_t i : m_shadowOrder) {
        const LightData& light = view.lights[i];
        switch (light.type) {
            case LightType::Directional: fitDirectional(light, i, cam, next2D, haveSun); break;
            case LightType::Spot:        fitSpot(light, i, next2D);                      break;
            case LightType::Point:       fitPoint(light, i, nextCube);                   break;
            default: break;
        }
    }
    fitTilesToAtlas();

    // Otherwise a refused light just stops casting, which reads as a content bug, not a budget.
    // Latched, so an over-budget scene says so once.
    if (m_refused > 0 && !m_budgetLogged) {
        LOG_WARNING(
            "%u shadow-casting light(s) found no free atlas tile (%u 2D, of which %u are "
            "the sun's cascades, and %u cube); they render without a shadow",
            m_refused,
            Config::MAX_SHADOW_CASTERS_2D,
            Config::NUM_CASCADES,
            Config::MAX_SHADOW_CASTERS_CUBE
        );
    }
    m_budgetLogged = m_refused > 0;

    cullCasters(view, glView);
}

namespace {

// Whether an AABB touches a sphere: the closest-point-on-box test.
bool aabbIntersectsSphere(const Math::AABB& bounds, const glm::vec3& center, float radius) {
    const glm::vec3 closest = glm::clamp(center, bounds.min, bounds.max);
    const glm::vec3 delta   = closest - center;
    return glm::dot(delta, delta) <= radius * radius;
}

/**
 * @brief Cull @p source against @p viewProjection into @p batch, grouped by key.
 *
 * A counting sort, not std::sort: keys are small dense integers, so a histogram and a prefix
 * sum take two linear passes.
 *
 * @param batch          Output; its order (object indices) and runs are rebuilt.
 * @param scratch        Per-task workspace, reused across frames.
 * @param objects        The frame's objects, which the casters name.
 * @param casters        Per caster, its object: the scene list's caster prefix.
 * @param keys           ShadowRun::key per caster, precomputed.
 * @param uploads        Per caster, the uploads its picture comes from (mesh, and a cutout's
 *                       material and map), precomputed.
 * @param slotCount      The range of the keys' low digit; @p keys run to KEY_GROUPS times it.
 * @param source         Which casters to consider: every one, or a light's sphere survivors.
 * @param viewProjection The tile or face matrix to cull against.
 * @param cascade        Whether the job is a cascade: unsigned, and culled with no near
 *                       plane (see Shadow2DJob::cascade).
 */
void cullInto(
    ShadowCasterBatch& batch,
    CullScratch& scratch,
    const RenderObjects& objects,
    const uint32_t* casters,
    const std::vector<uint32_t>& keys,
    const std::vector<uint64_t>& uploads,
    uint32_t slotCount,
    const std::vector<uint32_t>& source,
    const glm::mat4& viewProjection,
    bool cascade
) {
    Math::Frustum frustum = Math::extractFrustum(viewProjection);
    if (cascade) {
        // A zero plane rejects nothing: the near one, index 4.
        frustum.normals[4]    = glm::vec3(0.0f);
        frustum.absNormals[4] = glm::vec3(0.0f);
        frustum.d[4]          = 0.0f;
    }
    const bool cacheable = !cascade;
    // A tile's picture is the matrix and the survivors, in order. A posed survivor's is in its
    // bones, not here, so such a batch is unsigned. Raster state changing a tile's depth (a depth
    // bias, a cull mode) must be hashed here too, or a held tile keeps the picture drawn without it.
    uint64_t signature = cacheable ? fnv1a64Bytes(&viewProjection, sizeof(viewProjection)) : 0;
    bool     posed     = false;

    // Pass 1: survivors, counting each key. Signed by mesh id, not key: masked keys are numbered
    // over the scene's masked casters, so one added anywhere renumbers the rest.
    const uint32_t keyCount = slotCount * KEY_GROUPS;
    const uint32_t posedKey = slotCount * POSED_GROUP;
    scratch.survivors.clear();
    scratch.counts.assign(keyCount, 0);
    for (uint32_t caster : source) {
        const uint32_t object = casters[caster];
        if (!Math::frustumIntersectsAABB(frustum, objects.bounds[object]))
            continue;
        scratch.survivors.push_back(caster);
        ++scratch.counts[keys[caster]];
        if (cacheable) {
            if (keys[caster] >= posedKey) posed = true;
            const uint32_t mesh = objects.draws[object].mesh.id();
            signature = fnv1a64Bytes(&mesh, sizeof(mesh), signature);
            signature = fnv1a64Bytes(&uploads[caster], sizeof(uploads[caster]), signature);
            signature = fnv1a64Bytes(&objects.models[object], sizeof(glm::mat4), signature);
        }
    }
    // Zero is the "always draw" mark: a posed survivor earns it, and a hash
    // that happens to land on it moves off it.
    batch.signature = (!cacheable || posed) ? 0 : (signature == 0 ? 1 : signature);

    batch.order.resize(scratch.survivors.size());
    batch.runs.clear();
    if (scratch.survivors.empty()) return;

    // Prefix sum turns the histogram into a write cursor per key, and every key
    // that counted anything is one run of the draw.
    uint32_t running = 0;
    for (uint32_t key = 0; key < keyCount; ++key) {
        const uint32_t n = scratch.counts[key];
        scratch.counts[key] = running;
        if (n > 0) batch.runs.push_back({ running, n, key, key >= posedKey });
        running += n;
    }

    // Pass 2: scatter each survivor's object into its key's run.
    for (uint32_t caster : scratch.survivors) batch.order[scratch.counts[keys[caster]]++] = casters[caster];
}

} // namespace

void GLShadowData::cullCasters(const RenderView& view, const GLView& glView) {
    PROFILE_SCOPE("Shadow/Cull");

    const RenderObjects& objects = *view.objects;
    const uint32_t*      casters = objects.scene.data();

    m_batches2D.resize(m_jobs2D.size());
    m_batchesCube.resize(m_jobsCube.size() * 6);

    // Every caster is a 2D job's candidate: cascades and spots have no cheaper volume to reject
    // against. Casters are the view's prefix, so non-casting meshes are never walked.
    {
        PROFILE_SCOPE("Shadow/Cull/Prepare");
        // Keys flattened once, so each job's grouping reads them linearly.
        const uint32_t count = objects.casterCount;
        m_casters.resize(count);
        m_meshKeys.resize(count);
        m_meshUploads.resize(count);
        m_maskedCasters.clear();
        // Each key's slot first; the group digit goes on once the slots are counted.
        uint32_t lastMesh = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const ObjectDraw& draw = objects.draws[casters[i]];
            m_casters[i] = i;
            // The handle names the asset, not the geometry (a re-cook keeps its id), so a held tile
            // is keyed by uploads: the mesh's, and a cutout's material and map.
            const GLMesh*     mesh     = glView.getMesh(draw.mesh);
            const GLMaterial* material = glView.getMaterial(draw.material);
            uint64_t upload = mesh ? mesh->uploadId() : 0;
            if (material && material->getType() == MaterialType::AlphaMask) {
                const uint64_t cut[2] = {material->uploadId(), glView.textureUploadId(material->albedoMap())};
                upload = fnv1a64Bytes(cut, sizeof(cut), fnv1a64Bytes(&upload, sizeof(upload)));
                m_maskedCasters.push_back(i);
            } else {
                m_meshKeys[i] = draw.mesh.id();
                lastMesh = std::max(lastMesh, draw.mesh.id());
            }
            m_meshUploads[i] = upload;
        }

        // Equal pairs side by side, so each takes one slot past the last mesh.
        const auto pairOf = [&](uint32_t i) {
            const ObjectDraw& draw = objects.draws[casters[i]];
            return std::make_pair(draw.material.id(), draw.mesh.id());
        };
        std::sort(
            m_maskedCasters.begin(),
            m_maskedCasters.end(),
            [&](uint32_t a, uint32_t b) { return pairOf(a) < pairOf(b); }
        );
        uint32_t slot = lastMesh;
        for (size_t k = 0; k < m_maskedCasters.size(); ++k) {
            const uint32_t i = m_maskedCasters[k];
            if (k == 0 || pairOf(i) != pairOf(m_maskedCasters[k - 1])) ++slot;
            m_meshKeys[i] = slot;
        }

        m_slotCount = count ? slot + 1 : 0;
        for (uint32_t i = 0; i < count; ++i) {
            const ObjectDraw& draw = objects.draws[casters[i]];
            const GLMesh*     mesh = glView.getMesh(draw.mesh);
            if (!mesh || !mesh->isSkinned()) continue;
            const uint32_t group = draw.skinCount > 0 ? POSED_GROUP : SKINNED_GROUP;
            m_meshKeys[i] += group * m_slotCount;
        }
    }

    // Serially: each light's sphere narrows the scene for its six face culls, which parallelise,
    // so there are only ever a couple of iterations here to overlap.
    {
        PROFILE_SCOPE("Shadow/Cull/Sphere");
        m_cubeCandidates.resize(m_jobsCube.size());
        for (size_t j = 0; j < m_jobsCube.size(); ++j) {
            const ShadowCubeJob& job = m_jobsCube[j];
            std::vector<uint32_t>& candidates = m_cubeCandidates[j];

            candidates.clear();
            for (const uint32_t i : m_casters) {
                if (aabbIntersectsSphere(objects.bounds[casters[i]], job.pos, job.range))
                    candidates.push_back(i);
            }
        }
    }

    // A task per tile and face, writing disjoint batches, so nothing synchronises.
    const size_t jobs2D    = m_jobs2D.size();
    const size_t cubeFaces = m_jobsCube.size() * 6;

    PROFILE_SCOPE("Shadow/Cull/Fork");
    m_scratch.resize(jobs2D + cubeFaces);

    // The view outlives the frame, so the tasks may hold it until the join.
    m_cullView = &view;

    // Swept serially with no workers to hand the tasks to, or from a caller that
    // is one (see ThreadPool::isWorkerThread).
    const size_t taskCount = jobs2D + cubeFaces;
    ThreadPool& pool = ThreadPool::get();
    if (pool.threadCount() == 0 || ThreadPool::isWorkerThread()) {
        for (size_t task = 0; task < taskCount; ++task) cullTask(task);
        return;
    }

    // One queue entry lending one task: nothing is allocated per tile or face.
    m_cullBatch.emplace(taskCount, m_cullTaskBody);
    pool.addBatch(*m_cullBatch);
}

void GLShadowData::cullTask(size_t task) {
    const RenderObjects& objects = *m_cullView->objects;
    const uint32_t*      casters = objects.scene.data();
    const size_t jobs2D = m_jobs2D.size();
    if (task < jobs2D) {
        cullInto(
            m_batches2D[task],
            m_scratch[task],
            objects,
            casters,
            m_meshKeys,
            m_meshUploads,
            m_slotCount,
            m_casters,
            m_jobs2D[task].lightVP,
            m_jobs2D[task].cascade
        );
        return;
    }
    // The flat cube-batch index, six per job, as batchCube(job, face) reads it.
    const size_t cubeBatch = task - jobs2D;
    const size_t job       = cubeBatch / 6;
    const size_t face      = cubeBatch % 6;
    cullInto(
        m_batchesCube[cubeBatch],
        m_scratch[task],
        objects,
        casters,
        m_meshKeys,
        m_meshUploads,
        m_slotCount,
        m_cubeCandidates[job],
        m_jobsCube[job].faceVP[face],
        false
    );
}

void GLShadowData::finishCull() {
    if (!m_cullBatch) return;
    ThreadPool::get().waitForBatch(*m_cullBatch);
    m_cullBatch.reset();
}

// Directional sun: N frustum-fit cascades into the first 2D slots.
void GLShadowData::fitDirectional(
    const LightData& light,
    uint32_t lightIndex,
    const CameraFrustum& cam,
    uint32_t& next2D,
    bool& haveSun
) {
    // cascadeSplits is a vec4 and fr[] is sized N+1 == 5, so 4 cascades max.
    static_assert(Config::NUM_CASCADES <= 4, "The cascade splits are one vec4");
    constexpr uint32_t N = Config::NUM_CASCADES;
    // There is one set of cascades, taken by the first directional build()
    // walks; a second is not a budget refusal, so it is not counted as one.
    if (haveSun) return;
    if (next2D + N > Config::MAX_SHADOW_CASTERS_2D) {
        ++m_refused;
        return;
    }
    haveSun = true;

    // RenderView clamps a directional source to pi/4, so its tangent is finite.
    const float tanSource = std::tan(light.sourceRadius);

    const uint32_t base = next2D;
    m_data.csmBase  = static_cast<int>(base);
    m_data.csmCount = static_cast<int>(N);

    // Capped to the sun's shadowDistance so the cascades pack tightly; past sunFar is unshadowed.
    const float sunFar = std::max(cam.nearDepth + 1.0f, std::min(cam.farDepth, light.shadowDistance));

    // Logarithmic split anchored at CASCADE_NEAR, as fractions of the near->far edge so they index
    // the frustum corners; the last caps at sunFar. Why that anchor: docs/reference/lighting.md.
    const float anchor = std::clamp(CASCADE_NEAR, cam.nearDepth, sunFar);
    float fr[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    for (uint32_t c = 1; c < N; ++c) {
        const float si = static_cast<float>(c) / static_cast<float>(N);
        const float d  = anchor * std::pow(sunFar / anchor, si);
        fr[c] = (d - cam.nearDepth) / (cam.farDepth - cam.nearDepth);
    }
    fr[N] = (sunFar - cam.nearDepth) / (cam.farDepth - cam.nearDepth);

    const glm::vec3 dir = glm::normalize(light.direction);
    const glm::vec3 up  = stableUp(dir);

    for (uint32_t c = 0; c < N; ++c) {
        glm::vec3 corners[8];
        for (int k = 0; k < 4; ++k) {
            corners[k]     = glm::mix(cam.nearCorners[k], cam.farCorners[k], fr[c]);
            corners[k + 4] = glm::mix(cam.nearCorners[k], cam.farCorners[k], fr[c + 1]);
        }
        glm::vec3 center(0.0f);
        for (const glm::vec3& p : corners) center += p;
        center *= 0.125f;

        // Bounding-sphere fit: stable under camera rotation, no shimmer from a
        // light-space AABB. Round the radius to damp it further.
        float radius = 0.0f;
        for (const glm::vec3& p : corners) radius = std::max(radius, glm::length(p - center));
        radius = std::ceil(radius * 64.0f) / 64.0f;

        // Drives the normal-offset bias so it scales with cascade density, and is the quantum the
        // centre snaps to below.
        const uint32_t tile       = c < FULL_CASCADES ? m_tileRes : m_tileRes / 2;
        const float    worldTexel = (2.0f * radius) / static_cast<float>(tile);

        // The sphere fit holds the size still as the camera turns; an unsnapped centre still
        // slides, crawling every edge and voiding a texel-measured bias. The basis is the light
        // direction alone, since the cascade's own view is built from the snapped centre.
        const glm::mat4 lightBasis = glm::lookAt(glm::vec3(0.0f), dir, up);
        glm::vec3 centerLight = glm::vec3(lightBasis * glm::vec4(center, 1.0f));
        centerLight.x = std::floor(centerLight.x / worldTexel) * worldTexel;
        centerLight.y = std::floor(centerLight.y / worldTexel) * worldTexel;
        center = glm::vec3(glm::inverse(lightBasis) * glm::vec4(centerLight, 1.0f));

        const float     zExtend = radius;  // pull the near plane back to catch occluders
        const glm::vec3 eye     = center - dir * (radius + zExtend);
        const glm::mat4 lView   = glm::lookAt(eye, center, up);
        const glm::mat4 lProj   = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + zExtend);
        const glm::mat4 lightVP = lProj * lView;

        const uint32_t slot = base + c;
        m_tileSizes.push_back(tile);
        m_tileOwners.push_back(light.entitySlot);
        Shadow2DGPU& e = m_data.s2d[slot];
        e.lightVP = lightVP;
        // z is the ortho depth range, so a depth difference read off the tile
        // becomes metres between a blocker and what it shades.
        e.params  = glm::vec4(light.shadowBias, worldTexel, 2.0f * radius + zExtend, tanSource);
        e.shape   = glm::vec4(light.shadowNormalBias, 0.0f, 0.0f, 0.0f);
        m_jobs2D.push_back({ lightVP, slot, true });

        m_data.cascadeSplits[static_cast<int>(c)] =
            cam.nearDepth + fr[c + 1] * (cam.farDepth - cam.nearDepth);
    }
    next2D += N;
    m_lightSlot[lightIndex] = m_data.csmBase;  // any >= 0 flags "this light has a shadow"
}

// Spot: one perspective map into the next free 2D slot.
void GLShadowData::fitSpot(const LightData& light, uint32_t lightIndex, uint32_t& next2D) {
    if (next2D >= Config::MAX_SHADOW_CASTERS_2D) {
        ++m_refused;
        return;
    }
    const uint32_t slot = next2D++;

    const glm::vec3 dir = glm::normalize(light.direction);
    const glm::vec3 up  = stableUp(dir);

    // Neither may be degenerate: a range at or below near gives far <= near, and a zero cone
    // divides by tan(0), writing infinities that NaN whatever samples the matrix.
    const float range = std::max(lightRange(light), Config::SHADOW_NEAR * 2.0f);
    const float fov = glm::clamp(
        2.0f * light.outerConeAngle * 1.1f,
        glm::radians(1.0f),
        glm::radians(170.0f)
    );

    const glm::mat4 lView   = glm::lookAt(light.position, light.position + dir, up);
    const glm::mat4 lProj   = glm::perspective(fov, 1.0f, Config::SHADOW_NEAR, range);
    const glm::mat4 lightVP = lProj * lView;

    const float tanHalfFov = std::tan(fov * 0.5f);
    m_tileSizes.push_back(spotTileSize(light, range));
    m_tileOwners.push_back(light.entitySlot);

    Shadow2DGPU& e = m_data.s2d[slot];
    e.lightVP = lightVP;
    // The source over the map's width one metre out: what the soft path
    // scales by the blocker's and receiver's distances to size a penumbra.
    // Its texel (y) is measured once its tile is settled, in fitTilesToAtlas.
    const float sourceUV = light.sourceRadius / (2.0f * tanHalfFov);
    e.params  = glm::vec4(light.shadowBias, 0.0f, range, sourceUV);
    e.shape   = glm::vec4(light.shadowNormalBias, Config::SHADOW_NEAR, tanHalfFov, 0.0f);
    m_jobs2D.push_back({ lightVP, slot, false });

    m_lightSlot[lightIndex] = static_cast<int>(slot);
}

uint32_t GLShadowData::spotTileSize(const LightData& light, float range) const {
    // The cone's bounding sphere: about its middle when narrow, about its cap's centre when wide.
    const glm::vec3 dir  = glm::normalize(light.direction);
    const float     cosA = std::cos(light.outerConeAngle);
    const float     sinA = std::sin(light.outerConeAngle);
    float           radius;
    glm::vec3       centre;
    if (cosA > glm::one_over_root_two<float>()) {
        radius = range / (2.0f * cosA);
        centre = light.position + dir * radius;
    } else {
        radius = range * sinA;
        centre = light.position + dir * (range * cosA);
    }

    // The pixels its diameter covers; all of the largest tile for an eye inside it.
    const float distance = glm::length(centre - m_eye);
    float       pixels   = std::numeric_limits<float>::max();
    if (m_orthographic) {
        pixels = 2.0f * radius * m_focalPixels;
    } else if (distance > radius) {
        pixels = 2.0f * radius / std::sqrt(distance * distance - radius * radius) * m_focalPixels;
    }
    const float    texels = pixels * SPOT_TEXELS_PER_PIXEL;
    const float    wanted = std::min(texels, static_cast<float>(m_tileRes));
    const uint32_t size   = std::clamp(ceilPowerOfTwo(wanted), SHADOW_ATLAS_MIN_TILE_RES, m_tileRes);

    const auto held = std::find_if(m_spotTiles.begin(), m_spotTiles.end(), [&](const SpotTile& tile) {
        return tile.entitySlot == light.entitySlot;
    });
    if (held != m_spotTiles.end() && size < held->size && held->size <= m_tileRes
        && texels > SPOT_SHRINK_BELOW * static_cast<float>(held->size)) {
        return held->size;
    }
    return size;
}

void GLShadowData::fitTilesToAtlas() {
    uint64_t area = 0;
    for (const uint32_t size : m_tileSizes) area += static_cast<uint64_t>(size) * size;

    // The largest spot halves first, the later of two equal ones; a cascade keeps its size.
    const uint64_t capacity = GLShadowAtlas::capacity(m_tileRes);
    while (area > capacity) {
        uint32_t largest = 0;
        bool     found   = false;
        for (const Shadow2DJob& job : m_jobs2D) {
            const uint32_t size = m_tileSizes[job.slot];
            if (job.cascade || size <= SHADOW_ATLAS_MIN_TILE_RES) continue;
            if (!found || size >= m_tileSizes[largest]) {
                largest = job.slot;
                found   = true;
            }
        }
        if (!found) break;
        const uint64_t was = m_tileSizes[largest];
        m_tileSizes[largest] /= 2;
        area -= was * was - static_cast<uint64_t>(m_tileSizes[largest]) * m_tileSizes[largest];
    }

    m_spotTilesNext.clear();
    for (const Shadow2DJob& job : m_jobs2D) {
        if (job.cascade) continue;
        // World texel size at the range, for the normal-offset bias; the shader takes it to the
        // receiver's own distance.
        Shadow2DGPU& e = m_data.s2d[job.slot];
        e.params.y = 2.0f * e.params.z * e.shape.z / static_cast<float>(m_tileSizes[job.slot]);
        m_spotTilesNext.push_back({m_tileOwners[job.slot], m_tileSizes[job.slot]});
    }
    std::swap(m_spotTiles, m_spotTilesNext);
}

void GLShadowData::placeTiles(const GLShadowAtlas& atlas) {
    for (const Shadow2DJob& job : m_jobs2D) m_data.s2d[job.slot].atlas = atlas.tileUV(job.slot);
}

// Point: six perspective faces into the next free cube slot.
void GLShadowData::fitPoint(const LightData& light, uint32_t lightIndex, uint32_t& nextCube) {
    if (nextCube >= Config::MAX_SHADOW_CASTERS_CUBE) {
        ++m_refused;
        return;
    }
    const uint32_t slot = nextCube++;

    // Held off the near plane for the same reason fitSpot holds its own: a cube
    // face whose far plane is its near plane has no projection.
    const float range = std::max(lightRange(light), Config::SHADOW_NEAR * 2.0f);
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, Config::SHADOW_NEAR, range);

    ShadowCubeJob job;
    job.pos   = light.position;
    job.range = range;
    job.slot  = slot;
    for (int f = 0; f < 6; ++f) {
        job.faceVP[f] = proj * GLCubemap::faceView(f, light.position);
    }
    m_jobsCube.push_back(job);

    ShadowCubeGPU& e = m_data.scube[slot];
    e.posRange = glm::vec4(light.position, range);
    e.params   = glm::vec4(light.shadowBias, Config::SHADOW_NEAR, light.shadowNormalBias, light.sourceRadius);

    m_lightSlot[lightIndex] = static_cast<int>(slot);
}

void GLShadowData::uploadAndBind() {
    Vkm::GL::uploadIfChanged(m_ubo, m_last, m_data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::SHADOW);
}

} // namespace Vkm::Engine
