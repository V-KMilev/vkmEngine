#include "actions/asset_drop.h"

#include <optional>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>

#include "base/math/ray.h"
#include "core/frame_context.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/landscape/terrain.h"
#include "ecs/component/render/mesh.h"
#include "resource/resource_manager.h"
#include "core/frame/visibility.h"

#include "actions/create_entity.h"
#include "editor_context.h"
#include "editor_state.h"
#include "ui/asset/asset_drag.h"
#include "undo/component_edit.h"
#include "view/view_placement.h"
#include "view/viewport_projection.h"

namespace Vkm::Engine {

namespace {

// Assignment, through an EditScope: an undo step, and a prefab override on an instance.

bool meshTarget(const Scene& scene, EntityId id) { return scene.has<Mesh>(id); }

void assignMaterial(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<Mesh> mesh(scene, resources, state, id, "Assign Material");
    mesh->material = MaterialHandle{key};
}

void assignMesh(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id, StorageIndex key) {
    EditScope<Mesh> mesh(scene, resources, state, id, "Assign Mesh");
    mesh->mesh = MeshHandle{key};
}

// One Animator carries both the rig and its clip.
bool animatorTarget(const Scene& scene, EntityId id) { return scene.has<Animator>(id); }

void assignSkeleton(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<Animator> animator(scene, resources, state, id, "Assign Skeleton");
    animator->skeleton = SkeletonHandle{key};
}

void assignClip(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id, StorageIndex key) {
    EditScope<Animator> animator(scene, resources, state, id, "Assign Clip");
    animator->clip = AnimationClipHandle{key};
    // Cut, not crossFadeTo: the editor does not advance the clock a blend runs on.
    animator->time = 0.0f;
}

bool audioTarget(const Scene& scene, EntityId id) { return scene.has<AudioSource>(id); }

bool terrainTarget(const Scene& scene, EntityId id) { return scene.has<Terrain>(id); }

void assignTerrain(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<Terrain> terrain(scene, resources, state, id, "Assign Terrain");
    terrain->terrain = TerrainHandle{key};
}

void assignSound(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    StorageIndex key
) {
    EditScope<AudioSource> source(scene, resources, state, id, "Assign Sound");
    source->clip = AudioClipHandle{key};
}

/**
 * @brief What an asset of a kind is given to, and how.
 */
struct Assignment {
    AssetType type;
    bool (*target)(const Scene&, EntityId);
    void (*assign)(Scene&, ResourceManager&, EditorState&, EntityId, StorageIndex);
};

constexpr Assignment ASSIGNMENTS[] = {
    {AssetType::Material,      &meshTarget,     &assignMaterial},
    {AssetType::Mesh,          &meshTarget,     &assignMesh},
    {AssetType::Skeleton,      &animatorTarget, &assignSkeleton},
    {AssetType::AnimationClip, &animatorTarget, &assignClip},
    {AssetType::AudioClip,     &audioTarget,    &assignSound},
    {AssetType::Terrain,       &terrainTarget,  &assignTerrain},
};

const Assignment* assignmentOf(AssetType type) {
    for (const Assignment& given : ASSIGNMENTS) {
        if (given.type == type) return &given;
    }
    return nullptr;
}

/// How far what @p drag makes reaches below its origin: a mesh stands on its base.
float dropBase(const ResourceManager& resources, const AssetDrag& drag) {
    return drag.type == AssetType::Mesh ? ViewPlacement::meshBase(resources, MeshHandle{drag.key}) : 0.0f;
}

} // namespace

bool assignsTo(AssetType type, const Scene& scene, EntityId entity) {
    const Assignment* given = assignmentOf(type);
    return given && given->target(scene, entity);
}

void assignAsset(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity,
    AssetType type,
    StorageIndex key
) {
    if (const Assignment* given = assignmentOf(type)) given->assign(scene, resources, state, entity, key);
}

void acceptViewportDrop(EditorContext& ec) {
    const ImVec2 min = ec.viewportPos;
    const ImRect rect(min, ImVec2(min.x + ec.viewportSize.x, min.y + ec.viewportSize.y));
    if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##AssetDrop"))) return;

    // Peeked first, so a drag that would neither assign nor make anything here is not taken.
    FrameContext& ctx = ec.frame;
    const std::optional<AssetDrag> peeked = peekAssetDrag();
    if (peeked && ctx.visibility && ctx.visibility->hasCamera) {
        const AssetDrag drag = *peeked;
        const glm::mat4& invViewProj = ctx.visibility->camera.invViewProj;
        const Math::Ray ray = viewportRay(invViewProj, ImGui::GetMousePos(), min, ec.viewportSize);

        EntityId hit{};
        float t = 0.0f;
        ViewPlacement::firstSurface(ctx.scene, ctx.resources, ray, t, {}, &hit);
        // What the ray meets is a drawn surface; the entity a kind assigns to may hold it, as an
        // imported character's Animator holds its skinned meshes. A mesh dropped on a mesh is a
        // new thing set on it, never the floor turned into a crate.
        EntityId target{};
        if (hit && drag.type != AssetType::Mesh) {
            HierarchyOperations::forSelfAndAncestors(ctx.scene, hit, [&](EntityId at) {
                if (assignsTo(drag.type, ctx.scene, at)) target = at;
                return !target;
            });
        }
        if (target) {
            if (ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
                assignAsset(ctx.scene, ctx.resources, ec.state, target, drag.type, drag.key);
                ec.state.selectEntity(target);
            }
        } else if (EditorActions::makesEntity(drag.type) && ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
            const float base   = dropBase(ctx.resources, drag);
            const float ahead  = ec.state.viewAim.focus;
            const glm::vec3 at = ViewPlacement::placeAlong(ctx.scene, ctx.resources, ray, ahead, base);
            EditorActions::createFromAsset(ctx.scene, ctx.resources, ec.state, drag.type, drag.key, at);
        }
    }
    ImGui::EndDragDropTarget();
}

void acceptAssetOn(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity) {
    // Peeked first, so a drag of a kind that does not assign here is not taken (nor the row lit).
    const std::optional<AssetDrag> drag = peekAssetDrag();
    if (!drag) return;
    if (!assignsTo(drag->type, scene, entity)) return;
    if (ImGui::AcceptDragDropPayload(ASSET_DRAG)) {
        assignAsset(scene, resources, state, entity, drag->type, drag->key);
        state.selectEntity(entity);
    }
}

void acceptAssetCreate(Scene& scene, ResourceManager& resources, EditorState& state) {
    // Peeked first: only a kind an entity is made of is taken, so a material is not swallowed.
    const std::optional<AssetDrag> drag = peekAssetDrag();
    if (!drag || !EditorActions::makesEntity(drag->type) || !ImGui::AcceptDragDropPayload(ASSET_DRAG)) return;
    const float     base = dropBase(resources, *drag);
    const glm::vec3 at   = ViewPlacement::placeInView(scene, resources, state.viewAim, base);
    EditorActions::createFromAsset(scene, resources, state, drag->type, drag->key, at);
}

} // namespace Vkm::Engine
