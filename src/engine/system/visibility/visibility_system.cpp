#define VKM_LOG_CATEGORY "VISIBILITY"

#include "system/visibility/visibility_system.h"

#include <algorithm>
#include <atomic>

#include <glm/gtc/constants.hpp>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "base/hash/fnv1a.h"
#include "base/profile/profiler.h"
#include "vkm_platform/threading/thread_pool.h"
#include "vkm_platform/window/window_manager.h"

#include "resource/resource_manager.h"
#include "ecs/scene.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/landscape/water.h"

#include "core/host_chrome.h"
#include "base/math/aabb.h"
#include "ecs/water/water_data.h"
#include "core/frame/pose_buffer.h"
#include "io/project/render_settings.h"
#include "core/frame/terrain_frame.h"
#include "system/visibility/culling.h"
#include "core/frame/host_view.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief An object's key (RenderObjects::keys): its entity, and @p part, which tells apart the
 *        objects one entity draws.
 *
 * @tparam Part  Hashed by its bytes.
 * @param entity The entity that draws it.
 * @param part   Which of that entity's objects it is.
 * @return The key.
 */
template <typename Part>
uint64_t objectKey(EntityId entity, const Part& part) {
    const uint64_t named = fnv1a64Bytes(&entity, sizeof(entity));
    return fnv1a64Bytes(&part, sizeof(part), named);
}

/**
 * @brief The box this frame's pose actually occupies, in the mesh's own space.
 *
 * The bind-pose box under-sizes a posed character, and an under-sized box culls
 * visible geometry. The posed bone-origin box is padded by `skinRadius` times
 * the largest bone scale; measured in mesh space, applied in rig space: exact
 * for a rigid bind transform, else conservative, as `maxBoneScale` is floored at 1.
 *
 * @param mesh Mesh being bounded.
 * @param slice The mesh's rig slice this frame, or null when nothing poses it.
 * @param outMin Filled with the low corner of the local-space box.
 * @param outMax Filled with the high corner.
 */
void poseLocalBounds(const MeshAsset& mesh, const PoseSlice* slice, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = mesh.boundsMin;
    outMax = mesh.boundsMax;
    if (!slice || slice->count == 0) return;

    const glm::vec3 pad(mesh.skinRadius * slice->maxBoneScale);
    outMin = slice->originMin - pad;
    outMax = slice->originMax + pad;
}

// What the cull records about each object, for the serial gather.
constexpr uint8_t STATE_DRAWN   = 1u << 0;  ///< Goes into the scene-wide list.
constexpr uint8_t STATE_VISIBLE = 1u << 1;  ///< The camera sees it.
constexpr uint8_t STATE_CASTS   = 1u << 2;  ///< Goes into the scene list's caster prefix.
constexpr uint8_t STATE_CHANGED = 1u << 3;  ///< Its columns or its lists differ from the last frame's.

// The bits that put an object in the scene-wide list and its caster prefix.
constexpr uint8_t STATE_LISTED = STATE_DRAWN | STATE_CASTS;

/// The version of @p mesh, or 0 when none is loaded.
uint64_t versionOf(const ResourceManager& resources, MeshHandle mesh) {
    const MeshAsset* asset = resources.tryGet(mesh);
    return asset ? asset->version() : 0;
}

/**
 * @brief The geometry an entity draws at a distance, and the level it hands over to there.
 */
struct LevelPick {
    MeshHandle mesh;  ///< ObjectDraw::mesh.
    ObjectFade fade;  ///< Across a hand-over, the next level and how far it has faded in.
};

/**
 * @brief Pick the geometry for this entity at this distance.
 *
 * Distance is to the bounds center, not the origin, so an object whose pivot
 * sits far from its body does not pop. Past a level's range, for Culling::FADE_BAND of it, the
 * next level fades in.
 *
 * @tparam LODStorage The LOD component's storage.
 * @param mesh       Its handle is the full-detail level, and the fallback.
 * @param lodStorage Null when the scene has no LOD components.
 * @param entityIdx  The entity's slot, which keys its LOD.
 * @param worldMin   World bounds' low corner.
 * @param worldMax   World bounds' high corner.
 * @param context    Supplies the camera position and LOD distance scale.
 * @return The chosen mesh, never empty when the Mesh component had one, and the hand-over.
 */
template <typename LODStorage>
LevelPick selectLOD(
    const Mesh& mesh,
    const LODStorage* lodStorage,
    uint32_t entityIdx,
    const glm::vec3& worldMin,
    const glm::vec3& worldMax,
    const VisibilityContext& context
) {
    if (!lodStorage || !lodStorage->contains(entityIdx)) return {mesh.mesh, {}};

    const LOD& lod = lodStorage->get(entityIdx);
    if (lod.levels.empty()) return {mesh.mesh, {}};

    const glm::vec3 center = (worldMin + worldMax) * 0.5f;
    const glm::vec3 delta  = center - context.cameraPosition;
    const float distance = glm::length(delta);
    const float bias     = glm::max(lod.bias, glm::epsilon<float>());
    const float scaled   = distance * context.lodDistanceScale / bias;

    const auto meshOf = [&](size_t level) {
        return lod.levels[level].mesh ? lod.levels[level].mesh : mesh.mesh;
    };
    for (size_t level = 0; level < lod.levels.size(); ++level) {
        const float range = lod.levels[level].maxDistance;
        if (scaled <= range) return {meshOf(level), {}};
        const float band = range * Culling::FADE_BAND;
        if (level + 1 < lod.levels.size() && scaled < range + band) {
            const MeshHandle next = meshOf(level + 1);
            if (next == meshOf(level)) return {next, {}};
            const float share = (scaled - range) / band;
            return {meshOf(level), {next, share, share}};
        }
    }

    // Past the last threshold the coarsest level keeps drawing, to the camera's far plane.
    return {meshOf(lod.levels.size() - 1), {}};
}

/**
 * @brief How the eye draws an object it sees: as its level hands over, and fading out as it shrinks
 *        toward the screen-size cull within Culling::FADE_BAND over it.
 *
 * Shrinking in a hand-over, both levels fade out together, each keeping its share of what shows.
 *
 * @param level The hand-over its LOD picked, if any.
 * @param over  Its size over the cull (Culling::sizeOverCull), 1 or more.
 * @return Its fade.
 */
ObjectFade eyeFade(const ObjectFade& level, float over) {
    const float shown = (over - 1.0f) / Culling::FADE_BAND;
    if (shown >= 1.0f) return level;
    return {level.next, 1.0f - (1.0f - level.amount) * shown, level.taken * shown};
}

/**
 * @brief How large an object the eye may see is, over the screen-size cull (Culling::sizeOverCull):
 *        seen at 1 or more, and 0 where the frustum drops it.
 *
 * @param frustum The frustum it is seen through.
 * @param bounds  Its world box.
 * @param context Supplies the camera and the culls' thresholds.
 * @return Its size over the cull, or 0.
 */
float seenSize(const Math::Frustum& frustum, const Math::AABB& bounds, const VisibilityContext& context) {
    if (!Math::frustumIntersectsAABB(frustum, bounds)) return 0.0f;
    return Culling::sizeOverCull(bounds, context);
}

// What the cull keeps about a fixed mesh between frames (VisibilitySystem::m_fixed).
constexpr uint8_t FIXED_KEPT       = 1u << 0;  ///< Its columns are kept; zero is "work it out".
constexpr uint8_t FIXED_DRAWN      = 1u << 1;  ///< As STATE_DRAWN.
constexpr uint8_t FIXED_CASTS      = 1u << 2;  ///< As STATE_CASTS.
constexpr uint8_t FIXED_NOT_EYE    = 1u << 3;  ///< Seen by all but the eye: never visible.
constexpr uint8_t FIXED_LOD_SELECT = 1u << 4;  ///< An LOD component picks its mesh each frame.
constexpr uint8_t FIXED_WORLD      = 1u << 5;  ///< Its matrix is its WorldTransform's.
constexpr uint8_t FIXED_WATER      = 1u << 6;  ///< It is water: its draw names its Water's place.

// How far a plant group's distance moves before its levels are picked again, in meters.
constexpr float PLANT_REACH_STEP = 2.0f;

// Seconds a plant group's new levels take to fade in over the old: none pops as the eye walks by.
constexpr float PLANT_HANDOVER_SECONDS = 0.5f;

/**
 * @brief What working out one object reads, beyond its index.
 */
struct ObjectInputs {
    const Scene&                     scene;
    const ResourceManager&           resources;
    const SparseSet<Mesh>&           meshes;
    const SparseSet<Transform>&      transforms;
    const SparseSet<WorldTransform>* worldTransforms;  ///< Null when no entity ever had one.
    const SparseSet<LOD>*            lods;             ///< Null when no entity ever had one.
    const SparseSet<Water>*          waters;           ///< Null when no entity ever had one.
    const PoseBuffer*                poses;            ///< Null when nothing is posed.
    const std::vector<uint8_t>*      hidden;           ///< By slot, the host hides it; null for none.
    const VisibilityContext&         context;
    glm::vec2                        swell;            ///< How far the waves move water, across and up.
    bool                             throughAnEye;     ///< The frame is seen through a scene camera.
};

/**
 * @brief One object's columns of RenderObjects, and what the cull records about it.
 */
struct ObjectColumns {
    glm::mat4  model{1.0f};
    Math::AABB bounds;
    ObjectDraw draw;
    ObjectFade fade;                ///< Across a hand-over, how the eye draws it if it sees it.
    uint32_t   skinFirst  = 0;
    EntityId   entity{};
    uint8_t    state      = 0;      ///< STATE_DRAWN and STATE_CASTS.
    bool       notTheEye  = false;  ///< Seen by all but the eye, and the frame is the eye's.
    bool       posedWater = false;  ///< Water drawn as its material: on a posed mesh, or past the last.
    uint8_t    fixedState = 0;      ///< What m_fixed keeps for it; zero unless it is fixed.
};

/**
 * @brief Work out object @p index's columns from its components and assets.
 *
 * @param in    What it is worked out from.
 * @param index Its place in the Mesh storage.
 * @param out   Filled when it draws.
 * @return Whether it draws at all: shown, with a mesh and material, a Transform and a valid box.
 */
bool computeObject(const ObjectInputs& in, uint32_t index, ObjectColumns& out) {
    const uint32_t entityIdx = in.meshes.keyAt(index);
    const Mesh& mesh = in.meshes.dataAt(index);

    if (!mesh.visible) return false;
    if (in.hidden && entityIdx < in.hidden->size() && (*in.hidden)[entityIdx]) return false;
    // Nothing to shade it with draws and casts nothing, decided once for every list.
    if (!mesh.mesh || !mesh.material) return false;
    if (!in.transforms.contains(entityIdx)) return false;

    const auto& meshAsset = in.resources.get(mesh.mesh);
    if (!meshAsset.bounds().valid()) return false;

    const bool hasWorld = in.worldTransforms && in.worldTransforms->contains(entityIdx);
    out.model = hasWorld
        ? in.worldTransforms->get(entityIdx).model
        : Transform::computeModelMatrix(in.transforms.get(entityIdx));

    // The pose sizes a skinned mesh's box; an unskinned one keeps its bind bounds.
    const bool skinned     = !meshAsset.skin.empty();
    const EntityId entity  = in.scene.entityAt(entityIdx);
    const PoseSlice* slice = (in.poses && skinned) ? in.poses->sliceOf(entity) : nullptr;
    glm::vec3 localMin, localMax;
    poseLocalBounds(meshAsset, slice, localMin, localMax);

    out.bounds = Math::transform(out.model, {localMin, localMax});
    // Water is drawn as water on a static mesh alone; its waves carry it past its box by its share
    // of as far as they move water, and a picture held of it would freeze one moment of them.
    const bool  isWater = in.waters && in.waters->contains(entityIdx);
    const bool  water   = isWater && !skinned && in.waters->indexOf(entityIdx) < ObjectDraw::MAX_WATERS;
    out.posedWater = isWater && !water;
    const float share   = water ? std::max(in.waters->get(entityIdx).waves, 0.0f) : 0.0f;
    const bool  waves   = share > 0.0f;
    if (waves) {
        const glm::vec3 reach = share * glm::vec3(in.swell.x, in.swell.y, in.swell.x);
        out.bounds.min -= reach;
        out.bounds.max += reach;
    }

    const bool viewmodel = in.throughAnEye && mesh.seenBy == SeenBy::Viewmodel;
    // Kept between frames: what does not move, is not skinned, no waves move and is no held weapon.
    const bool fixed = !mesh.movable && !skinned && !waves && !viewmodel;
    const LevelPick level = selectLOD(mesh, in.lods, entityIdx, out.bounds.min, out.bounds.max, in.context);
    const auto named = water ? static_cast<uint16_t>(in.waters->indexOf(entityIdx) + 1u) : uint16_t{0};
    out.fade      = level.fade;
    out.draw      = ObjectDraw{level.mesh, mesh.material, slice ? slice->count : 0u, named, viewmodel, fixed};
    out.skinFirst = slice ? slice->first : 0u;
    out.entity    = entity;

    const bool eyeOnly = viewmodel || (in.throughAnEye && mesh.seenBy == SeenBy::EyeOnly);
    out.notTheEye = in.throughAnEye && mesh.seenBy == SeenBy::AllButEye;
    out.state     = eyeOnly ? 0 : STATE_DRAWN;
    if (mesh.castShadows && !waves && !eyeOnly) out.state |= STATE_CASTS;

    out.fixedState = 0;
    if (fixed) {
        out.fixedState = FIXED_KEPT;
        if (out.state & STATE_DRAWN) out.fixedState |= FIXED_DRAWN;
        if (out.state & STATE_CASTS) out.fixedState |= FIXED_CASTS;
        if (out.notTheEye)           out.fixedState |= FIXED_NOT_EYE;
        if (in.lods && in.lods->contains(entityIdx) && !in.lods->get(entityIdx).levels.empty()) {
            out.fixedState |= FIXED_LOD_SELECT;
        }
        if (hasWorld) out.fixedState |= FIXED_WORLD;
        if (water)    out.fixedState |= FIXED_WATER;
    }
    return true;
}

/**
 * @brief Whether a kept fixed mesh's entity still has the components it was worked out from.
 *
 * @param in    What it is worked out from.
 * @param index Its place in the Mesh storage.
 * @param fixed What m_fixed keeps for it.
 * @return False when it gained or lost a Transform, a WorldTransform or a picking LOD, or is or was
 *         water, whose place in its storage may have moved with another's.
 */
bool keepsItsComponents(const ObjectInputs& in, uint32_t index, uint8_t fixed) {
    const uint32_t entityIdx = in.meshes.keyAt(index);
    const bool world = in.worldTransforms && in.worldTransforms->contains(entityIdx);
    const bool picks = in.lods && in.lods->contains(entityIdx) && !in.lods->get(entityIdx).levels.empty();
    const bool water = (in.waters && in.waters->contains(entityIdx)) || (fixed & FIXED_WATER);
    return in.transforms.contains(entityIdx) && world == ((fixed & FIXED_WORLD) != 0)
        && picks == ((fixed & FIXED_LOD_SELECT) != 0) && !water;
}

/**
 * @brief Whether object @p i's columns from last frame still hold what @p now works out.
 *
 * @param now        Its columns worked out this frame.
 * @param objects    Last frame's columns.
 * @param i          The object's index.
 * @param levelPicks Its LOD picks the mesh it draws each frame, so the mesh drawn is not compared.
 * @return True when its model, bounds, draw and first bone are as they were.
 */
bool columnsHold(const ObjectColumns& now, const RenderObjects& objects, uint32_t i, bool levelPicks) {
    ObjectDraw draw = now.draw;
    if (levelPicks) draw.mesh = objects.draws[i].mesh;
    return now.model == objects.models[i] && draw == objects.draws[i] && now.skinFirst == objects.skinFirst[i]
        && now.bounds.min == objects.bounds[i].min && now.bounds.max == objects.bounds[i].max;
}

/**
 * @brief Size every column of RenderObjects, and Visibility::entities beside them, to @p total objects.
 *
 * @param result The cull's product.
 * @param total  Objects this frame.
 */
void resizeColumns(Visibility& result, uint32_t total) {
    RenderObjects& objects = result.objects;
    objects.models.resize(total);
    objects.bounds.resize(total);
    objects.draws.resize(total);
    objects.skinFirst.resize(total);
    objects.keys.resize(total);
    objects.shapes.resize(total);
    objects.bases.resize(total);
    objects.fades.resize(total);
    result.entities.resize(total);
}

/**
 * @brief Write object @p i's columns of RenderObjects, and its entity, but its fade, which each kind of
 *        object works out its own way.
 *
 * @param result The cull's product.
 * @param i      The object's index.
 * @param now    Its model, bounds, draw, first bone and entity.
 * @param base   The mesh it is given, whatever level it draws (RenderObjects::bases).
 * @param shape  Its base's version (versionOf).
 * @param key    What names it whatever its index (objectKey).
 */
void writeColumns(
    Visibility& result,
    uint32_t i,
    const ObjectColumns& now,
    const MeshHandle& base,
    uint64_t shape,
    uint64_t key
) {
    RenderObjects& objects = result.objects;
    objects.models[i]    = now.model;
    objects.bounds[i]    = now.bounds;
    objects.draws[i]     = now.draw;
    objects.skinFirst[i] = now.skinFirst;
    objects.keys[i]      = key;
    objects.shapes[i]    = shape;
    objects.bases[i]     = base;
    result.entities[i]   = now.entity;
}

} // namespace

void VisibilitySystem::publishCamera(
    const Camera& camera,
    const CameraView& seen,
    float viewportAspect,
    EntityId entity
) {
    CameraData& data = m_result.camera;
    data                         = CameraData::from(seen.view, seen.projection, seen.position);
    data.viewmodelViewProjection = Camera::computeViewmodelProjection(camera, viewportAspect) * data.view;

    m_result.cameraEntity = entity;
    m_result.hasCamera    = true;
}

bool VisibilitySystem::resolveCamera(const FrameContext& ctx, float viewportAspect) {
    const Scene& scene   = ctx.scene;
    const HostView* host = ctx.hostView;
    if (host && !host->through) {
        const CameraView seen = viewThrough(host->camera, host->position, host->rotation, viewportAspect);
        publishCamera(host->camera, seen, viewportAspect, {});
        return true;
    }

    // No fallback without an active camera: the frame draws nothing rather than
    // inheriting a host's last view.
    const EntityId entity = host ? host->through : ctx.camera.of(scene);
    // A camera parented to a rig renders from its world pose, not its local offset.
    const std::optional<CameraView> seen = viewThrough(scene, entity, viewportAspect);
    if (!seen) return false;
    publishCamera(scene.get<Camera>(entity), *seen, viewportAspect, entity);
    return true;
}

void VisibilitySystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("VisibilitySystem");

    // Cleared here, not at the gather, so early returns publish an empty result, not last frame's.
    RenderObjects& objects = m_result.objects;
    objects.visible.clear();
    objects.scene.clear();
    objects.casterCount = 0;
    // An early return below leaves the columns as they were but untracked: whole, both now and
    // on the next frame that writes them.
    objects.changed.clear();
    objects.allChanged    = true;
    m_result.hasCamera    = false;
    m_result.cameraEntity = {};

    // Cameras in auto-aspect mode (aspect <= 0) track the viewport.
    const HostChrome::ViewportRect viewport = ctx.chrome.viewportIn(ctx.window);
    const float vpW            = static_cast<float>(viewport.width);
    const float vpH            = static_cast<float>(viewport.height);
    const float viewportAspect = vpH > 0.0f ? vpW / vpH : 16.0f / 9.0f;

    if (!resolveCamera(ctx, viewportAspect)) {
        // A supported state (see RenderView::build); logged on the edge only, and not before the
        // world has run at all: a behavior's onStart, which may make the camera, has not either.
        const bool notRunYet = ctx.clock.getTick() == 0 && ctx.clock.isSimulationStill();
        if (!ctx.hostView && !m_noCameraLogged && !notRunYet) {
            LOG_WARNING("No active camera found for visibility");
            m_noCameraLogged = true;
        }
        m_sources.reset();
        ctx.visibility = &m_result;
        return;
    }
    m_noCameraLogged = false;

    const CameraData& eye = m_result.camera;

    const float projScaleY        = eye.projection[1][1];
    const float denom             = projScaleY * vpH;
    const float screenThresholdSq = (denom > 0.0f)
        ? (Culling::MIN_PIXELS * Culling::MIN_PIXELS) / (denom * denom)
        : 0.0f;

    // A narrower view magnifies, so a level holds further out; orthographic keeps the reference.
    const bool perspective = eye.projection[3][3] == 0.0f;
    const bool magnifies   = perspective && projScaleY > 0.0f;
    const float lodDistanceScale = magnifies ? LOD::REFERENCE_P11 / projScaleY : 1.0f;

    VisibilityContext context{
        .frustum               = Math::extractFrustum(eye.viewProjection),
        .cameraPosition        = eye.position,
        .view                  = eye.view,
        .screenSizeThresholdSq = screenThresholdSq,
        .lodDistanceScale      = lodDistanceScale,
        .perspective           = perspective,
    };

    // The sparse sets directly: the cull iterates them by index, in parallel. A world of terrain
    // alone has no meshes, which is none to cull.
    static const SparseSet<Mesh> NO_MESHES;
    const auto* meshStorage           = ctx.scene.storage<Mesh>() ? ctx.scene.storage<Mesh>() : &NO_MESHES;
    const auto* transformStorage      = ctx.scene.storage<Transform>();
    const auto* worldTransformStorage = ctx.scene.storage<WorldTransform>();

    if (!transformStorage) {
        m_sources.reset();
        ctx.visibility = &m_result;
        return;
    }

    const uint32_t meshCount = static_cast<uint32_t>(meshStorage->size());

    const auto& resources   = ctx.resources;
    const auto* lodStorage  = ctx.scene.storage<LOD>();
    const PoseBuffer* poses = ctx.poses;

    // How far the waves carry water, which a surface with a share of them is grown by.
    const glm::vec2 swell  = ctx.scene.water().waves().surface.farthestReach();
    const auto* waterStore = ctx.scene.storage<Water>();

    // Mesh::seenBy names a scene camera's view; a host's free view sees everything.
    const bool throughAnEye = static_cast<bool>(m_result.cameraEntity);
    // A weapon held nearer than the world's near plane is still in its own projection's view.
    const Math::Frustum viewmodelFrustum = Math::extractFrustum(eye.viewmodelViewProjection);

    // What every object's columns are made from beyond its components. No material is read: a
    // material edited changes nothing here.
    const ObjectSources sources{
        ctx.scene.epoch(),
        resources.epoch(),
        resources.revisionOf<MeshAsset>(),
        throughAnEye
    };
    const ObjectMemberships memberships{
        transformStorage->structure(),
        worldTransformStorage ? worldTransformStorage->structure() : 0,
        lodStorage ? lodStorage->structure() : 0,
        waterStore ? waterStore->structure() : 0
    };
    // Last frame's columns compare index by index while the world and the asset graph hold; an
    // index whose stamp moved holds another Mesh, added or moved there by a removal.
    const bool comparable = m_sources && sources.sceneEpoch == m_sources->sceneEpoch
        && sources.assetEpoch == m_sources->assetEpoch;
    // A world the host may edit keeps nothing: the author moves fixed meshes too.
    const bool authoring = ctx.chrome.authoring;
    // What the host hides from its view while it authors, as a flag a slot.
    const std::vector<uint32_t>& hiddenSlots = ctx.chrome.hiddenInView;
    const bool hiding = authoring && !hiddenSlots.empty();
    if (hiding) {
        m_hidden.assign(*std::max_element(hiddenSlots.begin(), hiddenSlots.end()) + 1u, 0);
        for (const uint32_t slot : hiddenSlots) m_hidden[slot] = 1;
    }
    const bool keep    = comparable && sources == *m_sources && !authoring;
    const bool recheck = memberships != m_memberships;
    // The indices last frame's columns cover, as far as they compare.
    const uint32_t covered   = static_cast<uint32_t>(std::min<size_t>(m_stamps.size(), meshCount));
    const uint32_t lastCount = comparable ? covered : 0u;

    // A fixed mesh listed at an index past the end is gone, which no index below says.
    bool fixedGone = false;
    for (size_t i = meshCount; comparable && i < m_stamps.size(); ++i) {
        fixedGone = fixedGone || ((m_state[i] & STATE_LISTED) && objects.draws[i].fixed);
    }

    // The terrains' chunks follow the meshes, then their plants (TerrainSystem's product).
    const TerrainFrame* terrains = ctx.terrain;
    const auto chunkCount = terrains ? static_cast<uint32_t>(terrains->chunks.size()) : 0u;
    const auto plantCount = terrains ? static_cast<uint32_t>(terrains->plants.size()) : 0u;
    const uint32_t total = meshCount + chunkCount + plantCount;

    // Persistent: a steady scene allocates nothing. Each index's columns have one writer. What is
    // kept of a Mesh between frames covers the meshes alone.
    m_state.resize(total);
    m_fixed.resize(meshCount);
    m_stamps.resize(meshCount);
    resizeColumns(m_result, total);

    const ObjectInputs inputs{
        ctx.scene,
        resources,
        *meshStorage,
        *transformStorage,
        worldTransformStorage,
        lodStorage,
        waterStore,
        poses,
        hiding ? &m_hidden : nullptr,
        context,
        swell,
        throughAnEye
    };
    const uint32_t checkPhase = m_checkPhase;
    m_checkPhase = (m_checkPhase + 1) % FIXED_CHECK_STRIDE;
    std::atomic<bool> fixedMoved{false};
    std::atomic<bool> fixedChanged{fixedGone};
    std::atomic<bool> posedWater{false};

    {
        PROFILE_SCOPE("Visibility/Cull");
        Platform::parallelFor(meshCount, [&](size_t i) {
            const auto idx = static_cast<uint32_t>(i);
            const uint64_t stamp = meshStorage->stampAt(idx);
            // What this index held last frame, which may have been another Mesh.
            const bool held = i < lastCount && m_stamps[i] == stamp;
            const uint8_t listed = i < lastCount ? (m_state[i] & STATE_LISTED) : 0;

            uint8_t fixed = keep && held ? m_fixed[i] : 0;
            if ((fixed & FIXED_KEPT) && recheck && !keepsItsComponents(inputs, idx, fixed)) fixed = 0;
            // Another mesh given is another object, where a level of detail is not: worked out whole.
            if ((fixed & FIXED_KEPT) && meshStorage->dataAt(idx).mesh != objects.bases[i]) fixed = 0;

            if (fixed & FIXED_KEPT) {
                if (idx % FIXED_CHECK_STRIDE == checkPhase) {
                    ObjectColumns now;
                    const bool levelPicks = (fixed & FIXED_LOD_SELECT) != 0;
                    if (!computeObject(inputs, idx, now) || now.fixedState != fixed
                        || !columnsHold(now, objects, idx, levelPicks)) {
                        fixedMoved.store(true, std::memory_order_relaxed);
                    }
                }
                uint8_t state = 0;
                if (fixed & FIXED_DRAWN) state |= STATE_DRAWN;
                if (fixed & FIXED_CASTS) state |= STATE_CASTS;
                const Math::AABB& world = objects.bounds[i];
                ObjectFade fade;
                if (fixed & FIXED_LOD_SELECT) {
                    const LevelPick level = selectLOD(
                        meshStorage->dataAt(idx),
                        lodStorage,
                        meshStorage->keyAt(idx),
                        world.min,
                        world.max,
                        context
                    );
                    fade = level.fade;
                    if (level.mesh != objects.draws[i].mesh) {
                        objects.draws[i].mesh = level.mesh;
                        state |= STATE_CHANGED;
                    }
                }
                const float seen = (fixed & FIXED_NOT_EYE) ? 0.0f : seenSize(context.frustum, world, context);
                if (seen >= 1.0f) state |= STATE_VISIBLE;
                objects.fades[i] = seen >= 1.0f ? eyeFade(fade, seen) : ObjectFade{};
                m_state[i]       = state;
                return;
            }

            m_stamps[i] = stamp;
            ObjectColumns now;
            const bool drawn = computeObject(inputs, idx, now);
            if (now.posedWater) posedWater.store(true, std::memory_order_relaxed);
            if (!drawn) {
                // Hidden now: a change while the index was listed, though its columns stay as they were.
                m_state[i] = listed ? STATE_CHANGED : 0;
                m_fixed[i] = 0;
                if (listed && objects.draws[i].fixed) fixedChanged.store(true, std::memory_order_relaxed);
                return;
            }

            // Last frame's columns are only comparable while their sources hold; when they do not,
            // RenderObjects::allChanged says so and nothing is listed. Another Mesh's never are.
            const bool changed  = comparable && !(held && columnsHold(now, objects, idx, false));
            const bool wasFixed = i < lastCount && objects.draws[i].fixed;

            const MeshHandle& given = meshStorage->dataAt(idx).mesh;
            const uint64_t    shape = versionOf(inputs.resources, given);
            writeColumns(m_result, idx, now, given, shape, objectKey(now.entity, given));
            m_fixed[i] = authoring ? 0 : now.fixedState;

            const Math::Frustum& frustum = now.draw.viewmodel ? viewmodelFrustum : context.frustum;
            uint8_t state = now.state;
            // Shown or hidden, or a caster no longer, is a change too: the lists hold it or not.
            if (changed || (comparable && ((state & STATE_LISTED) != listed))) state |= STATE_CHANGED;
            if ((state & STATE_CHANGED) && (wasFixed || now.draw.fixed)) {
                fixedChanged.store(true, std::memory_order_relaxed);
            }
            const float seen = now.notTheEye ? 0.0f : seenSize(frustum, now.bounds, context);
            if (seen >= 1.0f) state |= STATE_VISIBLE;
            objects.fades[i] = seen >= 1.0f ? eyeFade(now.fade, seen) : ObjectFade{};
            m_state[i]       = state;
        });
    }
    if (posedWater && !m_posedWaterLogged) {
        const uint32_t most = ObjectDraw::MAX_WATERS;
        LOG_WARNING("Water on a skinned mesh, or past the %u-th, draws as its material", most);
        m_posedWaterLogged = true;
    }

    // What ObjectDraw::water names.
    objects.waters.clear();
    if (waterStore) {
        objects.waters.reserve(waterStore->size());
        for (uint32_t w = 0; w < waterStore->size(); ++w) objects.waters.push_back(waterStore->dataAt(w));
    }

    // Each chunk at the level its distance from the eye picks. Fixed, as a terrain lies still: a
    // chunk remade or gone is a change of what the fixed objects draw; another level is not one.
    if (comparable && total < m_lastTotal) fixedChanged.store(true, std::memory_order_relaxed);
    for (uint32_t c = 0; c < chunkCount; ++c) {
        const TerrainChunk& chunk = terrains->chunks[c];
        const uint32_t i = meshCount + c;
        const glm::vec3 onBox = glm::clamp(context.cameraPosition, chunk.bounds.min, chunk.bounds.max);
        const float reach = glm::distance(context.cameraPosition, onBox) * context.lodDistanceScale;

        ObjectColumns now;
        now.model         = chunk.model;
        now.bounds        = chunk.bounds;
        now.draw.mesh     = chunk.levels[TerrainChunks::levelAt(reach, chunk.width)];
        now.draw.material = chunk.material;
        now.draw.fixed    = true;
        now.entity        = chunk.entity;
        const uint64_t shape = versionOf(resources, chunk.levels[0]);
        uint8_t state = chunk.castShadows ? STATE_DRAWN | STATE_CASTS : STATE_DRAWN;
        const bool remade = i >= m_lastTotal || now.draw.material != objects.draws[i].material
            || chunk.levels[0] != objects.bases[i] || shape != objects.shapes[i]
            || chunk.model != objects.models[i] || chunk.bounds.min != objects.bounds[i].min
            || chunk.bounds.max != objects.bounds[i].max;
        if (comparable && (remade || now.draw.mesh != objects.draws[i].mesh)) state |= STATE_CHANGED;
        if (comparable && remade) fixedChanged.store(true, std::memory_order_relaxed);
        writeColumns(m_result, i, now, chunk.levels[0], shape, objectKey(chunk.entity, chunk.levels[0]));
        objects.fades[i] = {};
        if (Math::frustumIntersectsAABB(context.frustum, chunk.bounds)) state |= STATE_VISIBLE;
        m_state[i] = state;
    }
    // Each plant listed always, so its index holds; seen within its layer's distance. Written only
    // when TerrainSystem listed them anew or they moved index; else each chunk's group is tested
    // first, and a plant only in a group in view.
    const uint32_t plantFirst = meshCount + chunkCount;
    const bool     plantsHeld = comparable && plantCount > 0 && terrains->plantsRevision == m_plantsRevision
        && plantFirst == m_plantFirst && plantCount == m_plantCount;
    const auto seenPlant = [&](const TerrainPlantDraw& plant) {
        const glm::vec3 center = 0.5f * (plant.bounds.min + plant.bounds.max);
        const bool inReach     = glm::distance(center, context.cameraPosition) <= plant.maxDistance;
        return inReach && Math::frustumIntersectsAABB(context.frustum, plant.bounds);
    };
    // A group's distance from the eye, as a level's reach counts it; a group picks its plants' levels
    // whole, so a walk changes what the fixed objects draw as seldom as a terrain's chunks do.
    const auto reachOf = [&](const TerrainPlantGroup& group) {
        const glm::vec3 onBox = glm::clamp(context.cameraPosition, group.bounds.min, group.bounds.max);
        return glm::distance(onBox, context.cameraPosition);
    };
    if (plantsHeld) {
        // A frame saying no time has passed, a step through frames, takes the new levels whole.
        const float seconds  = ctx.clock.getDeltaTime();
        const float handedOn = seconds > 0.0f ? seconds / PLANT_HANDOVER_SECONDS : 1.0f;
        for (size_t g = 0; g < terrains->plantGroups.size(); ++g) {
            const TerrainPlantGroup& group = terrains->plantGroups[g];
            const float reach  = reachOf(group);
            const float scaled = reach * context.lodDistanceScale;
            const bool inReach = reach <= group.maxDistance;
            const bool inView  = inReach && Math::frustumIntersectsAABB(context.frustum, group.bounds);
            PlantLevels& levels = m_plantLevels[g];
            // One hand-over at a time: a group picks again once the last has ended.
            const bool handing = levels.shown < 1.0f;
            if (handing) levels.shown = std::min(levels.shown + handedOn, 1.0f);
            const bool relevel = !handing && std::abs(scaled - levels.reach) > PLANT_REACH_STEP;
            if (relevel) levels = {scaled, levels.reach, seconds > 0.0f ? 0.0f : 1.0f};
            for (uint32_t p = group.first; p < group.first + group.count; ++p) {
                const TerrainPlantDraw& plant = terrains->plants[p];
                const uint32_t i = plantFirst + p;
                uint8_t state = m_state[i] & ~(STATE_VISIBLE | STATE_CHANGED);
                if (relevel || handing) {
                    // The old level is drawn, and the new one takes its samples, until it is whole.
                    const MeshHandle& next = plant.meshAt(levels.reach);
                    if (levels.shown >= 1.0f || plant.meshAt(levels.from) == next) {
                        if (objects.draws[i].mesh != next) state |= STATE_CHANGED;
                        objects.draws[i].mesh = next;
                        objects.fades[i]      = {};
                    } else {
                        objects.fades[i] = {next, levels.shown, levels.shown};
                    }
                }
                m_state[i] = inView && seenPlant(plant) ? state | STATE_VISIBLE : state;
            }
        }
    } else {
        m_plantLevels.resize(terrains ? terrains->plantGroups.size() : 0);
        size_t g = 0;
        // A terrain's plants are one run; each is keyed by its place in its own terrain's run, so
        // another terrain growing its plants again moves no key of this one's.
        uint32_t terrainFirst = 0;
        for (uint32_t p = 0; p < plantCount; ++p) {
            const TerrainPlantDraw& plant = terrains->plants[p];
            const uint32_t i = plantFirst + p;
            if (p > 0 && plant.entity != terrains->plants[p - 1].entity) terrainFirst = p;
            const TerrainPlantGroup* group = &terrains->plantGroups[g];
            if (p >= group->first + group->count) group = &terrains->plantGroups[++g];
            if (p == group->first) {
                const float scaled = reachOf(*group) * context.lodDistanceScale;
                m_plantLevels[g] = {scaled, scaled, 1.0f};
            }

            ObjectColumns now;
            now.model         = plant.model;
            now.bounds        = plant.bounds;
            now.draw.mesh     = plant.meshAt(m_plantLevels[g].reach);
            now.draw.material = plant.material;
            now.draw.fixed    = true;
            now.entity        = plant.entity;
            const uint64_t shape = versionOf(resources, plant.mesh);
            uint8_t state = plant.castShadows ? STATE_DRAWN | STATE_CASTS : STATE_DRAWN;
            const bool remade = i >= m_lastTotal || now.draw.material != objects.draws[i].material
                || plant.mesh != objects.bases[i] || shape != objects.shapes[i]
                || plant.model != objects.models[i];
            if (comparable && (remade || now.draw.mesh != objects.draws[i].mesh)) state |= STATE_CHANGED;
            if (comparable && remade) fixedChanged.store(true, std::memory_order_relaxed);
            writeColumns(m_result, i, now, plant.mesh, shape, objectKey(plant.entity, p - terrainFirst));
            objects.fades[i] = {};
            m_state[i]       = seenPlant(plant) ? state | STATE_VISIBLE : state;
        }
    }
    m_plantsRevision = terrains ? terrains->plantsRevision : 0;
    m_plantFirst     = plantFirst;
    m_plantCount     = plantCount;
    m_lastTotal      = total;

    if (fixedMoved.load(std::memory_order_relaxed)) {
        // Kept until the next frame, which works every object out again; the warning, once a world.
        if (m_fixedMovedEpoch != sources.sceneEpoch) {
            LOG_WARNING("A Mesh with movable off moved or changed while the world ran; kept state rebuilt");
            m_fixedMovedEpoch = sources.sceneEpoch;
        }
        m_sources.reset();
    } else {
        m_sources     = sources;
        m_memberships = memberships;
    }

    // Casters first, as the prefix RenderObjects::casterCount counts.
    PROFILE_SCOPE("Visibility/Gather");
    objects.allChanged = !comparable;
    ++objects.serial;
    const bool fixedMovedOn = objects.allChanged || fixedChanged.load(std::memory_order_relaxed);
    if (fixedMovedOn) ++objects.fixedSerial;
    for (uint32_t i = 0; i < total; ++i) {
        const uint8_t state = m_state[i];
        if (state & STATE_VISIBLE) objects.visible.push_back(i);
        if ((state & (STATE_DRAWN | STATE_CASTS)) == (STATE_DRAWN | STATE_CASTS)) objects.scene.push_back(i);
        if (state & STATE_CHANGED) objects.changed.push_back(i);
    }
    objects.casterCount = static_cast<uint32_t>(objects.scene.size());
    for (uint32_t i = 0; i < total; ++i) {
        if ((m_state[i] & (STATE_DRAWN | STATE_CASTS)) == STATE_DRAWN) objects.scene.push_back(i);
    }

    if (fixedMovedOn) {
        objects.levels.clear();
        const auto add = [&](const MeshHandle& level, const MeshHandle& given) {
            if (level && level != given) objects.levels.push_back({level, given});
        };
        for (uint32_t i = 0; lodStorage && i < meshCount; ++i) {
            if (!(m_state[i] & STATE_DRAWN) || !objects.draws[i].fixed) continue;
            const uint32_t key = meshStorage->keyAt(i);
            if (!lodStorage->contains(key)) continue;
            for (const LODLevel& level : lodStorage->get(key).levels) add(level.mesh, objects.bases[i]);
        }
        for (uint32_t c = 0; c < chunkCount; ++c) {
            const TerrainChunk& chunk = terrains->chunks[c];
            for (uint32_t level = 1; level < TerrainChunks::LEVELS; ++level) {
                add(chunk.levels[level], chunk.levels[0]);
            }
        }
        for (uint32_t p = 0; p < plantCount; ++p) {
            const TerrainPlantDraw& plant = terrains->plants[p];
            for (const MeshHandle& level : plant.levels) add(level, plant.mesh);
        }
        const auto order = [](const ObjectLevel& a, const ObjectLevel& b) {
            return a.given.id() != b.given.id() ? a.given.id() < b.given.id() : a.mesh.id() < b.mesh.id();
        };
        const auto same = [](const ObjectLevel& a, const ObjectLevel& b) {
            return a.mesh == b.mesh && a.given == b.given;
        };
        std::vector<ObjectLevel>& levels = objects.levels;
        std::sort(levels.begin(), levels.end(), order);
        levels.erase(std::unique(levels.begin(), levels.end(), same), levels.end());
    }

    ctx.visibility = &m_result;
}

} // namespace Vkm::Engine
