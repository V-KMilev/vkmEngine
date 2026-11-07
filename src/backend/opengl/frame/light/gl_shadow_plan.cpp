#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "frame/light/gl_shadow_plan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/round.hpp>

#include "vkm_log/assert.h"
#include "vkm_log/logger.h"
#include "vkm_opengl/buffer/buffer_upload.h"
#include "vkm_opengl/buffer/uniform_buffer.h"
#include "vkm_opengl/texture/cube_faces.h"

#include "asset/gl_asset_mirror.h"
#include "convention/gl_bindings.h"
#include "asset/gl_material.h"
#include "asset/gl_mesh.h"
#include "storage/light/gl_shadow_atlas.h"
#include "frame/light/gl_shadow_block.h"
#include "frame/light/gl_shadow_shape.h"
#include "base/math/axes.h"
#include "ecs/component/light/light.h"
#include "system/render/render_view.h"
#include "system/render/data/light_data.h"
#include "base/profile/profiler.h"
#include "vkm_platform/threading/thread_pool.h"
#include "base/hash/fnv1a.h"
#include "base/math/frustum.h"

namespace Vkm::Engine {

GLShadowPlan::GLShadowPlan() {
    for (int& s : m_lightSlot) s = -1;
}
GLShadowPlan::~GLShadowPlan() {
    // The tasks write into this object; none may outlive it.
    finishCull();
}

namespace {

// Spot/point shadow near plane: small enough not to clip inside a small light,
// large enough to keep depth resolution at typical occluder distances.
constexpr float SHADOW_NEAR = 0.1f;

// ShadowRun::key's high digit: whether fixed, vertex layout, and for a skinned caster whether posed.
// In draw order, so a tile's fixed casters lead its runs and are signed by them (cullInto), each
// layout is one stretch of them and each program one of a layout's.
constexpr uint32_t FIXED_GROUP   = 0;  ///< A fixed mesh (ObjectDraw::fixed): static layout.
constexpr uint32_t STATIC_GROUP  = 1;
constexpr uint32_t SKINNED_GROUP = 2;  ///< Skinned layout, drawn as stored.
constexpr uint32_t POSED_GROUP   = 3;
constexpr uint32_t KEY_GROUPS    = 4;
static_assert(
    FIXED_GROUP < STATIC_GROUP && STATIC_GROUP < SKINNED_GROUP && SKINNED_GROUP < POSED_GROUP
    && POSED_GROUP < KEY_GROUPS
);

// World distance the logarithmic cascade split is anchored at, rather than the
// camera's near plane, so raising shadowDistance costs the far field's density, not the
// foreground's sharpness.
constexpr float CASCADE_NEAR = 1.0f;

// A held layer's window moves over the grid in steps of its edge over this, and draws what a step
// uncovers: fewer steps draw less often, wider strips of what the layer shows.
constexpr uint32_t HELD_STEPS = 128;

// How many of a window's texels, past a step's, a cascade keeps outside its reach for the filter and
// the normal offset.
constexpr uint32_t WINDOW_MARGIN_TEXELS = 8;

// What depth a held layer spans past its fixed casters, in meters, so they are never on its ends.
constexpr float HELD_DEPTH_MARGIN = 1.0f;


// Shadow texels per screen pixel of a spot's cone.
constexpr float SPOT_TEXELS_PER_PIXEL = 1.27324f;

// A spot's tile halves only once its cone needs under this share of it: past half, by a margin.
constexpr float SPOT_SHRINK_BELOW = 0.4f;

// A caster whose box spans fewer texels of a tile or face than this is not drawn into it: its shadow
// there is a smudge the filter all but erases.
constexpr float MIN_CASTER_TEXELS = 10.0f;

// A movers' tile's filter (params.w, shaders/opengl/include/shadows.glsl): 1, the three-texel one, for a sun
// with a size while soft shadows are on; 0, one compare, otherwise.
float filterOf(bool softShadows, const LightData& light) {
    return softShadows && light.sourceRadius > 0.0f ? 1.0f : 0.0f;
}

} // namespace

void GLShadowPlan::build(const RenderView& view, const GLAssetMirror& mirror, uint32_t tileRes) {
    // A cull still in flight - a frame that threw between the plan and the
    // pass - is writing into the batches about to be reused.
    finishCull();

    m_jobs2D.clear();
    m_jobsCube.clear();
    m_jobsHeld.clear();
    m_data     = ShadowUBOData{};
    m_sunLight = -1;
    for (int& s : m_lightSlot) s = -1;
    m_lightCount = std::min<uint32_t>(static_cast<uint32_t>(view.lists.lights.size()), Config::MAX_LIGHTS);

    // Floored as the atlas floors it, so a tile is a tile. A new size is a new, empty, layer.
    const uint32_t res = std::max(tileRes, SHADOW_ATLAS_MIN_TILE_RES);
    if (res != m_tileRes) forgetHeld();
    m_tileRes = res;
    m_tileSizes.clear();
    m_spotTilesNext.clear();

    m_eye          = view.camera.position;
    m_orthographic = view.camera.projection[3][3] != 0.0f;
    m_focalPixels  = view.camera.projection[1][1] * 0.5f * static_cast<float>(view.sceneHeight());
    m_soft         = view.settings->softShadows;

    const glm::vec3 camPos = view.camera.position;

    uint32_t next2D   = 0;
    uint32_t nextCube = 0;

    // The sun first, because its cascades need Config::NUM_CASCADES consecutive tiles; then by
    // Light::shadowPriority, and nearness to the eye where tiles are short; ties by entity slot, not
    // `view.lists.lights` order, which is a SparseSet's packing and moves when an unrelated light is
    // destroyed.
    m_refused = 0;
    m_shadowOrder.clear();
    // A spot or point with no reach, or whose reach misses the view, lights nothing seen: no slot,
    // so one that is seen can have it.
    const Math::Frustum seen = Math::extractFrustum(view.camera.viewProjection);
    for (uint32_t i = 0; i < m_lightCount; ++i) {
        const LightData& light = view.lists.lights[i];
        if (!light.castShadows) continue;
        if (light.type == LightType::Spot || light.type == LightType::Point) {
            if (light.radius <= 0.0f) continue;
            const glm::vec3 extent(light.radius);
            if (!Math::frustumIntersectsAABB(seen, {light.position - extent, light.position + extent})) {
                continue;
            }
        }
        m_shadowOrder.push_back(i);
    }
    const auto sunFirst = [&](uint32_t i) {
        return view.lists.lights[i].type == LightType::Directional ? 0 : 1;
    };
    // Nearness ranks only a kind of tile more lights ask for than there are: while all fit, a light
    // keeps its tile however the camera moves, and a held tile its picture.
    bool sun = false;
    uint32_t spots  = 0;
    uint32_t points = 0;
    for (const uint32_t i : m_shadowOrder) {
        const LightType type = view.lists.lights[i].type;
        sun = sun || type == LightType::Directional;
        const bool cube = castsThroughCube(view.lists.lights[i]);
        spots  += type == LightType::Spot && !cube ? 1u : 0u;
        points += cube ? 1u : 0u;
    }
    const uint32_t spotTiles = Config::MAX_SHADOW_CASTERS_2D - (sun ? Config::NUM_CASCADES : 0u);
    const bool spotsShort    = spots > spotTiles;
    const bool pointsShort   = points > Config::MAX_SHADOW_CASTERS_CUBE;
    // A sun's priority and position say nothing: the lowest slot casts, as findKeyLight keys the sky.
    const auto priority = [&](uint32_t i) {
        return view.lists.lights[i].type == LightType::Directional ? 0 : view.lists.lights[i].shadowPriority;
    };
    const auto distanceSq = [&](uint32_t i) {
        const LightType type = view.lists.lights[i].type;
        const bool cube   = castsThroughCube(view.lists.lights[i]);
        const bool ranked = cube ? pointsShort : (type == LightType::Spot && spotsShort);
        if (!ranked) return 0.0f;
        const glm::vec3 d = view.lists.lights[i].position - camPos;
        return glm::dot(d, d);
    };
    std::sort(m_shadowOrder.begin(), m_shadowOrder.end(), [&](uint32_t a, uint32_t b) {
        if (sunFirst(a) != sunFirst(b)) return sunFirst(a) < sunFirst(b);
        if (priority(a) != priority(b)) return priority(a) > priority(b);
        if (distanceSq(a) != distanceSq(b)) return distanceSq(a) < distanceSq(b);
        return view.lists.lights[a].entitySlot < view.lists.lights[b].entitySlot;
    });

    for (const uint32_t i : m_shadowOrder) {
        const LightData& light = view.lists.lights[i];
        // A spot too wide for one map casts through a cube as a point light does.
        if (light.type == LightType::Directional) fitSun(view, mirror, light, i, next2D);
        else if (castsThroughCube(light))         fitPoint(light, i, nextCube);
        else if (light.type == LightType::Spot)   fitSpot(light, i, next2D);
    }
    std::swap(m_spotTiles, m_spotTilesNext);

    // The sun that casts here is the one a capture shadows (GLCaptureShadow).
    [[maybe_unused]] const LightData* captured = lowestSlotDirectionalCaster(view.lists.lights);
    VKM_ASSERT(
        m_sunLight < 0 || &view.lists.lights[static_cast<size_t>(m_sunLight)] == captured,
        "The cascaded sun is not lowestSlotDirectionalCaster"
    );

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

    cullCasters(view, mirror);
}

namespace {

// What @p object's shadow key is made from.
KeyedCaster keyedOf(const RenderObjects& objects, uint32_t object) {
    const ObjectDraw& draw = objects.draws[object];
    return {object, draw.mesh.id(), draw.material.id(), draw.fixed, draw.skinCount > 0};
}

// @p matrix as the words a signature takes (fnv1a64Words).
std::array<uint64_t, 8> wordsOf(const glm::mat4& matrix) {
    static_assert(sizeof(glm::mat4) == 8 * sizeof(uint64_t), "A matrix is eight words");
    std::array<uint64_t, 8> words;
    std::memcpy(words.data(), &matrix, sizeof(matrix));
    return words;
}

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
 * Signs the batch as ShadowCasterBatch::signature describes.
 *
 * @param batch          Output; its order (object indices) and runs are rebuilt.
 * @param scratch        Per-task workspace, reused across frames.
 * @param objects        The frame's objects, which the casters name.
 * @param casters        Per caster, its object: the scene list's caster prefix.
 * @param keys           ShadowRun::key per caster, precomputed.
 * @param uploads        Per caster, the uploads its picture comes from (mesh, and a cutout's
 *                       material and map), precomputed.
 * @param slotCount      The range of the keys' low digit; @p keys run to KEY_GROUPS times it.
 * @param source         Which casters to consider, by position: null for every one.
 * @param moving         Null to test @p source; else the moving ones of it, alone tested, the
 *                       fixed survivors kept from the batch's last cull (fixedSurvivors).
 * @param viewProjection The tile or face matrix to cull against.
 * @param edge           The tile's or face's edge, in texels: a caster spanning fewer than
 *                       MIN_CASTER_TEXELS of them is culled.
 * @param cascade        Whether the job is a cascade: culled with no near plane (see
 *                       Shadow2DJob::cascade).
 */
void cullInto(
    ShadowCasterBatch& batch,
    CullScratch& scratch,
    const RenderObjects& objects,
    const uint32_t* casters,
    const std::vector<uint32_t>& keys,
    const std::vector<uint64_t>& uploads,
    uint32_t slotCount,
    const std::vector<uint32_t>* source,
    const std::vector<uint32_t>* moving,
    const glm::mat4& viewProjection,
    uint32_t edge,
    bool cascade
) {
    Math::Frustum frustum = Math::extractFrustum(viewProjection);
    if (cascade) {
        // A zero plane rejects nothing: the near one, index 4.
        frustum.normals[4]    = glm::vec3(0.0f);
        frustum.absNormals[4] = glm::vec3(0.0f);
        frustum.d[4]          = 0.0f;
    }
    // Raster state changing a tile's depth (a depth bias, a cull mode) must be hashed here too,
    // or a held tile keeps the picture drawn without it.
    const std::array<uint64_t, 8> tile = wordsOf(viewProjection);
    const uint64_t size = edge;
    uint64_t fixedSignature = fnv1a64Words(tile.data(), tile.size());
    fixedSignature = fnv1a64Words(&size, 1, fixedSignature);
    fixedSignature = fnv1a64Words(&objects.fixedSerial, 1, fixedSignature);

    // A box's diagonal spans about diagonal * texels / w of the tile: texels is a world unit's at
    // w = 1, from the matrix's row for the tile's y; w is its last row's, 1 for a cascade.
    const glm::mat4 rows   = glm::transpose(viewProjection);
    const float     texels = 0.5f * static_cast<float>(edge) * glm::length(glm::vec3(rows[1]));
    const auto tooSmall = [&](const Math::AABB& box) {
        const float w = glm::dot(rows[3], glm::vec4(0.5f * (box.min + box.max), 1.0f));
        return w > 0.0f && glm::length(box.max - box.min) * texels < MIN_CASTER_TEXELS * w;
    };

    uint64_t movingSignature = FNV1A_OFFSET_BASIS;
    bool posed = false;

    // Pass 1: survivors, the fixed ones first when they are kept. Each key's survivors stay in
    // source order either way, so the batch is the same.
    const uint32_t keyCount  = slotCount * KEY_GROUPS;
    const uint32_t movingKey = slotCount * STATIC_GROUP;
    const uint32_t posedKey  = slotCount * POSED_GROUP;
    scratch.survivors.clear();
    if (moving) scratch.survivors = batch.fixedSurvivors;
    else        batch.fixedSurvivors.clear();
    const std::vector<uint32_t>* tested = moving ? moving : source;
    const size_t testedCount = tested ? tested->size() : keys.size();
    for (size_t k = 0; k < testedCount; ++k) {
        const uint32_t caster = tested ? (*tested)[k] : static_cast<uint32_t>(k);
        const Math::AABB& box = objects.bounds[casters[caster]];
        if (!Math::frustumIntersectsAABB(frustum, box) || tooSmall(box)) continue;
        scratch.survivors.push_back(caster);
        if (!moving && keys[caster] < movingKey) batch.fixedSurvivors.push_back(caster);
    }
    batch.culledFixedSerial = objects.fixedSerial;

    // Each key counted. Signed by mesh id, not key: masked keys are numbered over the scene's
    // masked casters, so one added anywhere renumbers the rest.
    scratch.counts.assign(keyCount, 0);
    scratch.firstOf.resize(movingKey);
    for (uint32_t caster : scratch.survivors) {
        const uint32_t object = casters[caster];
        const uint32_t key    = keys[caster];
        if (key < movingKey) {
            if (scratch.counts[key] == 0) scratch.firstOf[key] = caster;
        } else {
            const uint64_t drawn[2] = {objects.draws[object].mesh.id(), uploads[caster]};
            const std::array<uint64_t, 8> placed = wordsOf(objects.models[object]);

            posed           = posed || key >= posedKey;
            movingSignature = fnv1a64Words(drawn, 2, movingSignature);
            movingSignature = fnv1a64Words(placed.data(), placed.size(), movingSignature);
        }
        ++scratch.counts[key];
    }

    batch.order.resize(scratch.survivors.size());
    batch.runs.clear();
    batch.culledFor  = viewProjection;
    batch.culledEdge = edge;
    batch.culledAt   = objects.serial;

    // Prefix sum turns the histogram into a write cursor per key, and every key
    // that counted anything is one run of the draw. A fixed run is signed by its first caster's
    // mesh and uploads, which every caster of a run shares.
    uint32_t running = 0;
    for (uint32_t key = 0; key < keyCount; ++key) {
        const uint32_t n = scratch.counts[key];
        scratch.counts[key] = running;
        if (n > 0) {
            batch.runs.push_back({ running, n, key, key >= posedKey, key < movingKey });
            if (key < movingKey) {
                const uint32_t caster   = scratch.firstOf[key];
                const uint64_t drawn[2] = {objects.draws[casters[caster]].mesh.id(), uploads[caster]};
                fixedSignature = fnv1a64Words(drawn, 2, fixedSignature);
            }
        }
        running += n;
    }

    // Zero is the "always draw" mark: a posed survivor earns it, and a hash that happens to land
    // on it moves off it.
    const uint64_t whole = fnv1a64Words(&movingSignature, 1, fixedSignature);
    batch.signature = posed ? 0 : (whole == 0 ? 1 : whole);

    // Pass 2: scatter each survivor's object into its key's run.
    for (uint32_t caster : scratch.survivors) batch.order[scratch.counts[keys[caster]]++] = casters[caster];
}

} // namespace

void GLShadowPlan::cullCasters(const RenderView& view, const GLAssetMirror& mirror) {
    PROFILE_SCOPE("Shadow/Cull");

    const RenderObjects& objects = *view.objects;
    const uint32_t* casters = objects.scene.data();

    m_batches2D.resize(m_jobs2D.size());
    m_batchesCube.resize(m_jobsCube.size() * 6);
    m_batchesHeld.resize(m_jobsHeld.size());

    // Every caster is a 2D job's candidate: cascades and spots have no cheaper volume to reject
    // against. Casters are the view's prefix, so non-casting meshes are never walked. The keys are
    // the last frame's while every caster draws as it was keyed, none was rebuilt and no asset was
    // walked again: one that only moved keeps its key. With nothing changed, every caster is as it
    // was, and none is walked to say so.
    const auto drawnAsKeyed = [&] {
        for (uint32_t i = 0; i < objects.casterCount; ++i) {
            if (!(keyedOf(objects, casters[i]) == m_keyed[i])) return false;
        }
        return true;
    };
    const bool keysHold = !objects.allChanged && objects.serial == m_preparedSerial + 1
        && mirror.syncedRevision() == m_preparedRevision && objects.casterCount == m_keyed.size()
        && (objects.changed.empty() || drawnAsKeyed());
    m_preparedSerial   = objects.serial;
    m_preparedRevision = mirror.syncedRevision();
    if (!keysHold) {
        PROFILE_SCOPE("Shadow/Cull/Prepare");
        // Keys flattened once, so each job's grouping reads them linearly.
        const uint32_t count = objects.casterCount;
        m_keyed.resize(count);
        m_meshKeys.resize(count);
        m_meshUploads.resize(count);
        m_maskedCasters.clear();
        // Each key's slot first; the group digit goes on once the slots are counted. Props are
        // placed in runs, so a caster's mesh and material are mostly the last one's: an upload
        // key and a skin group are worked out once a run.
        m_meshGroups.resize(count);
        uint32_t lastMesh = 0;
        MeshHandle     runMesh;
        MaterialHandle runMaterial;
        uint64_t runUpload = 0;
        bool runMasked     = false;
        uint32_t runGroup  = STATIC_GROUP;
        for (uint32_t i = 0; i < count; ++i) {
            const ObjectDraw& draw = objects.draws[casters[i]];
            m_keyed[i] = keyedOf(objects, casters[i]);
            if (i == 0 || draw.mesh != runMesh || draw.material != runMaterial) {
                runMesh     = draw.mesh;
                runMaterial = draw.material;
                // The handle names the asset, not the geometry (a re-cook keeps its id), so a held
                // tile is keyed by uploads: the mesh's, and a cutout's material and map.
                const GLMesh* mesh = mirror.getMesh(draw.mesh);
                const GLMaterial* material = mirror.getMaterial(draw.material);
                runUpload = mesh ? mesh->uploadId() : 0;
                runMasked = material && material->cutsItsShadow();
                if (runMasked) {
                    const uint64_t map    = mirror.textureUploadId(material->albedoMap());
                    const uint64_t cut[2] = {material->uploadId(), map};
                    runUpload = fnv1a64Bytes(cut, sizeof(cut), fnv1a64Bytes(&runUpload, sizeof(runUpload)));
                }
                runGroup = mesh && mesh->isSkinned() ? SKINNED_GROUP : STATIC_GROUP;
            }
            if (runMasked) {
                m_maskedCasters.push_back(i);
            } else {
                m_meshKeys[i] = draw.mesh.id();
                lastMesh      = std::max(lastMesh, draw.mesh.id());
            }
            m_meshUploads[i] = runUpload;
            m_meshGroups[i]  = draw.fixed ? FIXED_GROUP
                : (runGroup == SKINNED_GROUP && draw.skinCount > 0 ? POSED_GROUP : runGroup);
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
        for (uint32_t i = 0; i < count; ++i) m_meshKeys[i] += m_meshGroups[i] * m_slotCount;
        m_movingCasters.clear();
        m_fixedCasters.clear();
        for (uint32_t i = 0; i < count; ++i) {
            (m_meshGroups[i] == FIXED_GROUP ? m_fixedCasters : m_movingCasters).push_back(i);
        }
    }

    m_data.moverCount = m_movingCasters.empty() ? 0 : m_data.csmCount;

    // With no object changed since the last frame, a tile or face culled as it is now then keeps
    // its batch: a still scene culls nothing.
    const bool still = keysHold && objects.changed.empty();
    const size_t jobs2D    = m_jobs2D.size();
    const size_t cubeFaces = m_jobsCube.size() * 6;
    const size_t jobsHeld  = m_jobsHeld.size();
    // Culled last frame against the same matrix, for a tile or face of the same size.
    const auto culledAs = [&](const ShadowCasterBatch& batch, const glm::mat4& matrix, size_t task) {
        return batch.culledAt + 1 == objects.serial && batch.culledFor == matrix
            && batch.culledEdge == edgeOf(task);
    };
    const auto keeps = [&](ShadowCasterBatch& batch, const glm::mat4& matrix, size_t task) {
        if (!still || !culledAs(batch, matrix, task)) return false;
        batch.culledAt = objects.serial;
        return true;
    };
    m_recull.assign(jobs2D + cubeFaces + jobsHeld, 1);
    for (size_t j = 0; j < jobs2D; ++j) m_recull[j] = !keeps(m_batches2D[j], m_jobs2D[j].lightVP, j);
    // A tile culled last frame as now, no fixed object changed since, tests only what moves: a
    // still light's spot, any still view.
    m_movingOnly.resize(jobs2D);
    for (size_t j = 0; j < jobs2D; ++j) {
        const ShadowCasterBatch& batch = m_batches2D[j];
        m_movingOnly[j] = keysHold && culledAs(batch, m_jobs2D[j].lightVP, j)
            && batch.culledFixedSerial == objects.fixedSerial;
    }
    for (size_t face = 0; face < cubeFaces; ++face) {
        const glm::mat4& matrix = m_jobsCube[face / 6].faceVP[face % 6];
        m_recull[jobs2D + face] = !keeps(m_batchesCube[face], matrix, jobs2D + face);
    }

    // Serially: each light's sphere narrows the scene for its six face culls, which parallelize,
    // so there are only ever a couple of iterations here to overlap.
    {
        PROFILE_SCOPE("Shadow/Cull/Sphere");
        m_cubeCandidates.resize(m_jobsCube.size());
        for (size_t j = 0; j < m_jobsCube.size(); ++j) {
            const auto first = m_recull.begin() + static_cast<std::ptrdiff_t>(jobs2D + j * 6);
            if (std::none_of(first, first + 6, [](uint8_t recull) { return recull != 0; })) continue;
            const ShadowCubeJob& job = m_jobsCube[j];
            std::vector<uint32_t>& candidates = m_cubeCandidates[j];

            candidates.clear();
            for (uint32_t i = 0; i < objects.casterCount; ++i) {
                if (aabbIntersectsSphere(objects.bounds[casters[i]], job.pos, job.range))
                    candidates.push_back(i);
            }
        }
    }

    // A task per tile and face, writing disjoint batches, so nothing synchronizes.
    PROFILE_SCOPE("Shadow/Cull/Fork");
    m_scratch.resize(jobs2D + cubeFaces + jobsHeld);

    // The tasks hold the view until the join, which the frame makes before it ends: the objects it
    // names are rewritten for the next.
    m_cullView = &view;

    // Swept serially with no workers to hand the tasks to, or from a caller that
    // is one (see ThreadPool::isWorkerThread).
    const size_t taskCount     = jobs2D + cubeFaces + jobsHeld;
    Platform::ThreadPool& pool = Platform::ThreadPool::get();
    if (pool.threadCount() == 0 || Platform::ThreadPool::isWorkerThread()) {
        for (size_t task = 0; task < taskCount; ++task) cullTask(task);
        return;
    }

    // One queue entry lending one task: nothing is allocated per tile or face.
    pool.addBatch(m_cullBatch, taskCount);
}

void GLShadowPlan::cullTask(size_t task) {
    if (!m_recull[task]) return;
    PROFILE_SCOPE("Shadow/Cull/Task");
    const RenderObjects& objects = *m_cullView->objects;
    const uint32_t* casters = objects.scene.data();
    const size_t jobs2D = m_jobs2D.size();
    if (task < jobs2D) {
        const Shadow2DJob& job = m_jobs2D[task];
        // A movers' tile draws what moves alone; a spot's whatever it sees.
        const bool only = !job.cascade && m_movingOnly[task];
        cullInto(
            m_batches2D[task],
            m_scratch[task],
            objects,
            casters,
            m_meshKeys,
            m_meshUploads,
            m_slotCount,
            job.cascade ? &m_movingCasters : nullptr,
            only ? &m_movingCasters : nullptr,
            job.lightVP,
            edgeOf(task),
            job.cascade
        );
        return;
    }
    const size_t cubeFaces = m_jobsCube.size() * 6;
    if (task >= jobs2D + cubeFaces) {
        const size_t held = task - jobs2D - cubeFaces;
        const ShadowHeldJob& job = m_jobsHeld[held];
        cullInto(
            m_batchesHeld[held],
            m_scratch[task],
            objects,
            casters,
            m_meshKeys,
            m_meshUploads,
            m_slotCount,
            &m_fixedCasters,
            nullptr,
            job.lightVP,
            job.rect.height,
            true
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
        &m_cubeCandidates[job],
        nullptr,
        m_jobsCube[job].faceVP[face],
        edgeOf(task),
        false
    );
}

uint32_t GLShadowPlan::edgeOf(size_t task) const {
    return task < m_jobs2D.size() ? m_tileSizes[m_jobs2D[task].slot] : SHADOW_CUBE_RES;
}

void GLShadowPlan::finishCull() {
    // Returns at once for a cull swept serially, which queued nothing.
    Platform::ThreadPool::get().waitForBatch(m_cullBatch);
}

glm::ivec2 GLShadowPlan::HeldLayer::windowAt(const glm::vec3& eye, uint32_t edge, uint32_t step) const {
    const glm::mat4  toLight = glm::lookAt(glm::vec3(0.0f), dir, Math::stableUp(dir));
    const glm::vec3  place   = glm::vec3(toLight * glm::vec4(eye, 1.0f));
    const glm::dvec2 grid    = glm::dvec2(place.x, place.y) / static_cast<double>(texel);
    const glm::dvec2 steps   = glm::floor(grid / static_cast<double>(step));
    return glm::ivec2(steps) * static_cast<int>(step) - glm::ivec2(static_cast<int>(edge / 2));
}

void GLShadowPlan::forgetHeld() {
    for (HeldLayer& held : m_held) held.drawn = false;
}

// The sun: what is fixed under it in held layers, what moves in tiles of the atlas.
void GLShadowPlan::fitSun(
    const RenderView& view,
    const GLAssetMirror& mirror,
    const LightData& light,
    uint32_t lightIndex,
    uint32_t& next2D
) {
    static_assert(Config::NUM_CASCADES <= 4, "The cascade splits are one vec4");
    constexpr uint32_t N = Config::NUM_CASCADES;
    // There is one set of cascades, taken by the first directional build()
    // walks; a second is not a budget refusal, so it is not counted as one.
    if (m_sunLight >= 0) return;
    if (next2D + N > Config::MAX_SHADOW_CASTERS_2D) {
        ++m_refused;
        return;
    }
    m_sunLight = static_cast<int>(lightIndex);

    m_data.csmBase  = static_cast<int>(next2D);
    m_data.csmCount = static_cast<int>(N);

    const RenderObjects& objects = *view.objects;
    if (objects.allChanged || objects.fixedSerial != m_fixedBoxSerial) {
        m_fixedBoxSerial = objects.fixedSerial;
        m_anyFixed       = false;
        for (uint32_t i = 0; i < objects.casterCount; ++i) {
            const uint32_t object = objects.scene[i];
            if (!objects.draws[object].fixed) continue;
            const Math::AABB& box = objects.bounds[object];
            m_fixedBox.min = m_anyFixed ? glm::min(m_fixedBox.min, box.min) : box.min;
            m_fixedBox.max = m_anyFixed ? glm::max(m_fixedBox.max, box.max) : box.max;
            m_anyFixed     = true;
        }
    }
    m_data.heldCount = m_anyFixed ? static_cast<int>(N) : 0;

    // How far from the eye each cascade reaches, past which is unshadowed: logarithmic, anchored at
    // CASCADE_NEAR. Measured from the eye, so a turn moves nothing.
    const float nearest = std::max(view.camera.zNear, 0.01f);
    const float sunFar  = std::max(nearest + 1.0f, std::min(view.camera.zFar, light.shadowDistance));
    const float anchor  = std::clamp(CASCADE_NEAR, nearest, sunFar);

    const glm::vec3 dir = glm::normalize(light.direction);
    // The fixed casters' reach along the sun, from their box's center.
    const glm::vec3 boxCenter = 0.5f * (m_fixedBox.min + m_fixedBox.max);
    const float boxReach = glm::dot(glm::abs(dir), 0.5f * (m_fixedBox.max - m_fixedBox.min));
    const float boxAlong = glm::dot(dir, boxCenter);

    // The window keeps a step and the filter outside the reach, whichever way it last stepped.
    const uint32_t edge    = 2 * m_tileRes;
    const float    margin  = static_cast<float>(edge / HELD_STEPS + WINDOW_MARGIN_TEXELS);
    const float    widened = static_cast<float>(edge) / (static_cast<float>(edge) - 2.0f * margin);

    for (uint32_t c = 0; c < N; ++c) {
        const float share = static_cast<float>(c + 1) / static_cast<float>(N);
        const float reach = c + 1 == N ? sunFar : anchor * std::pow(sunFar / anchor, share);
        m_data.cascadeSplits[static_cast<int>(c)] = reach;
        const float halfWidth = reach * widened;

        if (m_anyFixed) {
            HeldLayer wanted;
            wanted.dir         = dir;
            wanted.texel       = 2.0f * halfWidth / static_cast<float>(edge);
            wanted.nearDepth   = boxAlong - boxReach - HELD_DEPTH_MARGIN;
            wanted.farDepth    = boxAlong + boxReach + HELD_DEPTH_MARGIN;
            wanted.fixedSerial = objects.fixedSerial;
            wanted.revision    = mirror.syncedRevision();
            wanted.drawn       = true;
            fitHeld(c, wanted, objects.allChanged);
        }
        fitMovers(light, halfWidth, next2D);
    }
    m_heldTurn              = (m_heldTurn + 1) % N;
    m_lightSlot[lightIndex] = m_data.csmBase;  // any >= 0 flags "this light has a shadow"
}

void GLShadowPlan::fitHeld(uint32_t cascade, const HeldLayer& wanted, bool allChanged) {
    const uint32_t edge = 2 * m_tileRes;
    const uint32_t step = edge / HELD_STEPS;
    const int span = static_cast<int>(edge);

    HeldLayer& held = m_held[cascade];
    // Against the layer's own grid, which may lag the sun: it is read as what it shows.
    const glm::ivec2 window = held.drawn ? held.windowAt(m_eye, edge, step) : glm::ivec2(0);
    const glm::ivec2 moved  = window - held.window;
    const bool left   = !held.drawn || std::abs(moved.x) >= span || std::abs(moved.y) >= span;
    const bool remade = allChanged || held.fixedSerial != wanted.fixedSerial
        || held.revision != wanted.revision;
    const bool turned = held.dir != wanted.dir || held.texel != wanted.texel
        || held.nearDepth != wanted.nearDepth || held.farDepth != wanted.farDepth;

    // A layer the eye has left shows nothing it could use, nor one whose casters changed what they
    // are, so either is drawn now; one whose sun turned is right where it was, so it waits its
    // turn, and a sun that keeps turning draws one layer a frame.
    if (left || remade || (turned && cascade == m_heldTurn)) {
        held        = wanted;
        held.window = held.windowAt(m_eye, edge, step);
        addHeldRect(cascade, held.window, held.window + glm::ivec2(span));
    } else if (moved != glm::ivec2(0)) {
        const glm::ivec2 before = held.window;
        held.window = window;
        // The columns the move uncovered, whole; then the rows, less the corner those drew.
        if (moved.x > 0) addHeldRect(cascade, {before.x + span, window.y}, window + glm::ivec2(span));
        if (moved.x < 0) addHeldRect(cascade, window, {before.x, window.y + span});
        const int fromX = std::max(window.x, before.x);
        const int toX   = std::min(window.x, before.x) + span;
        if (moved.y > 0) addHeldRect(cascade, {fromX, before.y + span}, {toX, window.y + span});
        if (moved.y < 0) addHeldRect(cascade, {fromX, window.y}, {toX, before.y});
    }

    // Read wrapped, so any whole number of edges off is the same texel: the nearest keeps the
    // coordinates small. As addHeldRect's matrices do, a texel i spans [i, i + 1).
    const double width = static_cast<double>(span) * static_cast<double>(held.texel);
    const glm::dvec2 corner = glm::floor(glm::dvec2(held.window) / static_cast<double>(span)) * width;
    const float depth = held.farDepth - held.nearDepth;
    const glm::dmat4 toGrid = glm::dmat4(glm::lookAt(glm::vec3(0.0f), held.dir, Math::stableUp(held.dir)));
    const glm::dmat4 toTile = glm::ortho<double>(
        corner.x,
        corner.x + width,
        corner.y,
        corner.y + width,
        static_cast<double>(held.nearDepth),
        static_cast<double>(held.farDepth)
    );

    const glm::mat4 lightVP(toTile * toGrid);
    Shadow2DGPU& e = m_data.held[cascade];
    e       = Shadow2DGPU::orthographic(lightVP, held.texel, static_cast<float>(width), depth, 0.0f);
    e.atlas = GLShadowAtlas::heldUV(cascade);
}

void GLShadowPlan::addHeldRect(uint32_t cascade, const glm::ivec2& from, const glm::ivec2& to) {
    const HeldLayer& held = m_held[cascade];
    const int span = static_cast<int>(2 * m_tileRes);
    const glm::dmat4 grid = glm::dmat4(glm::lookAt(glm::vec3(0.0f), held.dir, Math::stableUp(held.dir)));
    const double size = static_cast<double>(held.texel);
    // Where the layer wraps: floored, as a grid coordinate runs below zero.
    const auto wrapAt = [&](int at) {
        return (at >= 0 ? at / span : (at - span + 1) / span) * span;
    };
    for (int y = from.y; y < to.y;) {
        const int yEnd = std::min(to.y, wrapAt(y) + span);
        for (int x = from.x; x < to.x;) {
            const int xEnd = std::min(to.x, wrapAt(x) + span);
            const glm::dmat4 over = glm::ortho<double>(
                x * size,
                xEnd * size,
                y * size,
                yEnd * size,
                static_cast<double>(held.nearDepth),
                static_cast<double>(held.farDepth)
            );
            const ShadowHeldRect rect{
                static_cast<uint32_t>(x - wrapAt(x)),
                static_cast<uint32_t>(y - wrapAt(y)),
                static_cast<uint32_t>(xEnd - x),
                static_cast<uint32_t>(yEnd - y)
            };
            m_jobsHeld.push_back({glm::mat4(over * grid), rect, cascade});
            x = xEnd;
        }
        y = yEnd;
    }
}

void GLShadowPlan::fitMovers(const LightData& light, float halfWidth, uint32_t& next2D) {
    const uint32_t  slot  = next2D++;
    const float     texel = 2.0f * halfWidth / static_cast<float>(m_tileRes);
    const glm::vec3 dir   = glm::normalize(light.direction);

    // Snapped to its texels, so what stands still under a moving eye keeps its edges.
    const glm::mat4 toLight = glm::lookAt(glm::vec3(0.0f), dir, Math::stableUp(dir));
    const glm::vec3 eye     = glm::vec3(toLight * glm::vec4(m_eye, 1.0f));
    const glm::vec2 center  = glm::floor(glm::vec2(eye) / texel) * texel;

    // Pulled back to catch what stands sunward of the eye; nearer than that flattens onto the plane.
    const float along = -eye.z;
    const float depth = 3.0f * halfWidth;
    const float zNear = along - 2.0f * halfWidth;

    const glm::mat4 lightVP = glm::ortho(
        center.x - halfWidth,
        center.x + halfWidth,
        center.y - halfWidth,
        center.y + halfWidth,
        zNear,
        zNear + depth
    ) * toLight;

    m_tileSizes.push_back(m_tileRes);
    const float filter = filterOf(m_soft, light);
    m_data.s2d[slot] = Shadow2DGPU::orthographic(lightVP, texel, 2.0f * halfWidth, depth, filter);

    const glm::vec2 half = glm::vec2(0.5f * static_cast<float>(m_tileRes));
    const glm::vec2 grid = glm::round(center / texel) - half;
    m_jobs2D.push_back({lightVP, slot, true, grid});
}

// Spot: one perspective map into the next free 2D slot.
void GLShadowPlan::fitSpot(const LightData& light, uint32_t lightIndex, uint32_t& next2D) {
    if (next2D >= Config::MAX_SHADOW_CASTERS_2D) {
        ++m_refused;
        return;
    }
    const uint32_t slot = next2D++;

    const glm::vec3 dir = glm::normalize(light.direction);
    const glm::vec3 up  = Math::stableUp(dir);

    // The range may not be degenerate: at or below near it gives far <= near, writing infinities
    // that NaN whatever samples the matrix.
    const float range = std::max(light.radius, SHADOW_NEAR * 2.0f);
    const float fov   = spotMapFov(light);

    const glm::mat4 lView   = glm::lookAt(light.position, light.position + dir, up);
    const glm::mat4 lProj   = glm::perspective(fov, 1.0f, SHADOW_NEAR, range);
    const glm::mat4 lightVP = lProj * lView;

    // texel: the world texel at the range, for the normal-offset bias; the shader takes it to the
    // receiver's own distance.
    const float tanHalfFov = std::tan(fov * 0.5f);
    const uint32_t size = spotTileSize(light, range);
    const float texel = 2.0f * range * tanHalfFov / static_cast<float>(size);
    m_tileSizes.push_back(size);
    m_spotTilesNext.push_back({light.entitySlot, size});

    Shadow2DGPU& e = m_data.s2d[slot];
    e.lightVP = lightVP;
    e.params  = glm::vec4(Light::SHADOW_BIAS, texel, range, 0.0f);
    e.shape   = glm::vec4(Light::SHADOW_NORMAL_BIAS, SHADOW_NEAR, tanHalfFov, 0.0f);
    m_jobs2D.push_back({ lightVP, slot, false });

    m_lightSlot[lightIndex] = static_cast<int>(slot);
}

uint32_t GLShadowPlan::spotTileSize(const LightData& light, float range) const {
    // The cone's bounding sphere: about its middle when narrow, about its cap's center when wide.
    const glm::vec3 dir  = glm::normalize(light.direction);
    const float     cosA = std::cos(light.outerConeAngle);
    const float     sinA = std::sin(light.outerConeAngle);
    float           radius;
    glm::vec3       center;
    if (cosA > glm::one_over_root_two<float>()) {
        radius = range / (2.0f * cosA);
        center = light.position + dir * radius;
    } else {
        radius = range * sinA;
        center = light.position + dir * (range * cosA);
    }

    // The pixels its diameter covers; all of the largest tile for an eye inside it.
    const float distance = glm::length(center - m_eye);
    float pixels = std::numeric_limits<float>::max();
    if (m_orthographic) {
        pixels = 2.0f * radius * m_focalPixels;
    } else if (distance > radius) {
        pixels = 2.0f * radius / std::sqrt(distance * distance - radius * radius) * m_focalPixels;
    }
    const float    texels = pixels * SPOT_TEXELS_PER_PIXEL;
    const float    wanted = std::min(texels, static_cast<float>(m_tileRes));
    const uint32_t texel  = static_cast<uint32_t>(std::ceil(std::fmax(wanted, 1.0f)));
    const uint32_t size   = std::clamp(glm::ceilPowerOfTwo(texel), SHADOW_ATLAS_MIN_TILE_RES, m_tileRes);

    const auto held = std::find_if(m_spotTiles.begin(), m_spotTiles.end(), [&](const SpotTile& tile) {
        return tile.entitySlot == light.entitySlot;
    });
    if (held != m_spotTiles.end() && size < held->size && held->size <= m_tileRes
        && texels > SPOT_SHRINK_BELOW * static_cast<float>(held->size)) {
        return held->size;
    }
    return size;
}

void GLShadowPlan::placeTiles(const GLShadowAtlas& atlas) {
    for (const Shadow2DJob& job : m_jobs2D) m_data.s2d[job.slot].atlas = atlas.tileUV(job.slot);
}

// Point: six perspective faces into the next free cube slot.
void GLShadowPlan::fitPoint(const LightData& light, uint32_t lightIndex, uint32_t& nextCube) {
    if (nextCube >= Config::MAX_SHADOW_CASTERS_CUBE) {
        ++m_refused;
        return;
    }
    const uint32_t slot = nextCube++;

    // Held off the near plane for the same reason fitSpot holds its own: a cube
    // face whose far plane is its near plane has no projection.
    const float range    = std::max(light.radius, SHADOW_NEAR * 2.0f);
    const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, SHADOW_NEAR, range);

    ShadowCubeJob job;
    job.pos   = light.position;
    job.range = range;
    job.slot  = slot;
    for (int f = 0; f < 6; ++f) {
        job.faceVP[f] = proj * Vkm::OpenGL::cubeFaceView(f, light.position);
    }
    m_jobsCube.push_back(job);

    ShadowCubeGPU& e = m_data.scube[slot];
    e.posRange = glm::vec4(light.position, range);
    e.params   = glm::vec4(Light::SHADOW_BIAS, SHADOW_NEAR, Light::SHADOW_NORMAL_BIAS, 0.0f);

    m_lightSlot[lightIndex] = static_cast<int>(slot);
}

void GLShadowPlan::uploadAndBind() {
    Vkm::OpenGL::uploadIfChanged(m_ubo, m_last, m_data);
    if (m_ubo) m_ubo->bindBase(GLBindings::UBOBindingPoints::SHADOW);
}

} // namespace Vkm::Engine
