#include "framework/editor_actions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>

#include <imgui.h>
#include <glm/glm.hpp>

#include "framework/editor_commands.h"
#include "framework/editor_state.h"
#include "framework/prefab_overrides.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "io/scene/prefab.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "system/visibility/visibility.h"
#include "core/math/bounds.h"
#include "system/camera/camera_controller_system.h"
#include "generator/light_generators.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"
#include "loader/model_loaders.h"
#include "io/project_paths.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {
namespace EditorActions {

void commitHierarchyMutation(EditorState& state) {
    state.hierarchyDirty = true;
    state.markSceneDirty();
}

namespace {
// Write a model matrix back into a local Transform (TRS), matching the gizmo's
// decomposition.
void setTransformFromMatrix(Transform& t, const glm::mat4& m) {
    const glm::vec3 cx(m[0]), cy(m[1]), cz(m[2]);
    t.position = glm::vec3(m[3]);
    t.scale    = glm::vec3(glm::length(cx), glm::length(cy), glm::length(cz));

    const glm::mat3 basis(
        t.scale.x != 0.0f ? cx / t.scale.x : glm::vec3(1.0f, 0.0f, 0.0f),
        t.scale.y != 0.0f ? cy / t.scale.y : glm::vec3(0.0f, 1.0f, 0.0f),
        t.scale.z != 0.0f ? cz / t.scale.z : glm::vec3(0.0f, 0.0f, 1.0f));
    t.rotation = glm::normalize(glm::quat_cast(basis));
}
} // namespace

void reparentKeepingWorld(Scene& scene, EditorState& state, EntityId child,
                          EntityId newParent, const char* label) {
    if (!scene.isAlive(child) || !scene.has<Transform>(child)) return;

    // An instance's interior belongs to its prefab: the scene stores the
    // instance as a reference and rebuilds the subtree from the file, so an
    // entity dropped inside one is never written, and one dragged out of one is
    // put back where the prefab has it. Both moves look like they worked and
    // are gone by the next load, so say no while there is still someone to tell.
    if (PrefabOverrides::instanceRoot(scene, newParent)) {
        state.pushToast(EditorState::ToastKind::Warning,
                        "A prefab instance is built from its prefab - an entity moved "
                        "into one is not saved with the scene");
        return;
    }
    const EntityId owningInstance = PrefabOverrides::instanceRoot(scene, child);
    if (owningInstance && owningInstance != child) {
        state.pushToast(EditorState::ToastKind::Warning,
                        "This entity belongs to a prefab - move the instance root, or "
                        "change the prefab and save it");
        return;
    }

    EntityId oldParent{};
    if (scene.has<Hierarchy>(child)) oldParent = scene.get<Hierarchy>(child).parent;

    // The world matrix is what stays fixed across the move; `before` is for undo.
    const Transform before = scene.get<Transform>(child);
    const glm::mat4 world   = HierarchyOperations::computeWorldMatrix(scene, child);

    const bool toParent = newParent && scene.isAlive(newParent);
    if (toParent) {
        HierarchyOperations::setParent(scene, child, newParent);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }

    // Re-express the preserved world matrix in the new parent's space so the
    // entity stays put. Unparenting to root leaves local == world.
    glm::mat4 local = world;
    if (toParent && scene.has<Transform>(newParent)) {
        local = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, newParent)) * world;
    }
    Transform& t = scene.get<Transform>(child);
    setTransformFromMatrix(t, local);

    state.commands.push(std::make_unique<ReparentCommand>(
        child, oldParent, toParent ? newParent : EntityId{}, before, t, label));
    commitHierarchyMutation(state);
}

void commitStructureChange(EditorState& state) {
    state.hierarchyDirty = true;
    state.markSceneDirty();
}

namespace {
// First material name not already taken: `base`, then "base 2", "base 3", ...
// The name is a material's identity (it's what save/load key off), so each new
// or duplicated material must land on a distinct one.
std::string uniqueMaterialName(ResourceManager& resources, const std::string& base) {
    std::string name = base;
    for (int n = 2; resources.findByName<MaterialAsset>(name); ++n) {
        name = base + " " + std::to_string(n);
    }
    return name;
}
} // namespace

MaterialHandle duplicateMaterial(
    ResourceManager& resources,
    EditorState& state,
    MaterialHandle source,
    Mesh* assignTo
) {
    if (!source) return MaterialHandle{};
    const MaterialAsset& src = resources.get(source);

    MaterialAsset copy = src;  // value copy of params + texture refs
    copy.version = 1;
    copy.name = uniqueMaterialName(resources,
        (src.name.empty() ? std::string("material") : src.name) + " copy");

    MaterialHandle nh = resources.add(std::move(copy));
    if (!nh) return MaterialHandle{};

    if (assignTo) assignTo->material = nh;
    state.markSceneDirty();
    return nh;
}

MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state) {
    MaterialHandle h = generateDefaultMaterial(resources);
    if (!h) return MaterialHandle{};

    // generateDefaultMaterial shares the "material:default" name; the unique
    // rename is what distinguishes each new material on save/load.
    resources.rename(h, uniqueMaterialName(resources, "Material"));

    state.markSceneDirty();
    return h;
}

namespace {
const char* defaultName(EntityKind k) {
    switch (k) {
        case EntityKind::Empty:            return "Empty";
        case EntityKind::Cube:             return "Cube";
        case EntityKind::Sphere:           return "Sphere";
        case EntityKind::Plane:            return "Plane";
        case EntityKind::Triangle:         return "Triangle";
        case EntityKind::Pyramid:          return "Pyramid";
        case EntityKind::Cone:             return "Cone";
        case EntityKind::PointLight:       return "Point Light";
        case EntityKind::SpotLight:        return "Spot Light";
        case EntityKind::DirectionalLight: return "Directional Light";
        case EntityKind::RectLight:        return "Rect Light";
        case EntityKind::DiskLight:        return "Disk Light";
        case EntityKind::Camera:           return "Camera";
        case EntityKind::ReflectionProbe:  return "Reflection Probe";
        case EntityKind::IrradianceVolume: return "Irradiance Volume";
        case EntityKind::Decal:            return "Decal";
        case EntityKind::ParticleEmitter:  return "Particle Emitter";
        case EntityKind::UICanvas:         return "UI Canvas";
        case EntityKind::UIPanel:          return "UI Panel";
        case EntityKind::UIText:           return "UI Text";
        case EntityKind::UIButton:         return "UI Button";
    }
    return "Entity";
}
}

EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityKind kind) {
    const EntityId entity = scene.createEntity();

    // UI entities are screen-space: a canvas gets a UICanvas, a UI element gets a
    // UIElement; everything else gets a 3D Transform.
    const bool isCanvas    = kind == EntityKind::UICanvas;
    const bool isUIElement = kind == EntityKind::UIPanel || kind == EntityKind::UIText
                          || kind == EntityKind::UIButton;
    if (isCanvas)         scene.add(entity, UICanvas{});
    else if (isUIElement) scene.add(entity, UIElement{});
    else                  scene.add(entity, Transform{});

    scene.add(entity, makeName(defaultName(kind)));

    auto addMesh = [&](MeshAsset mesh) {
        auto meshHandle = resources.add(std::move(mesh));
        auto matHandle  = generateDefaultMaterial(resources);
        scene.add(entity, Mesh{meshHandle, matHandle});
    };

    switch (kind) {
        case EntityKind::Empty:
            break;
        case EntityKind::PointLight:
            scene.add(entity, generateLight(LightType::Point));
            break;
        case EntityKind::SpotLight:
            scene.add(entity, generateLight(LightType::Spot));
            break;
        case EntityKind::DirectionalLight:
            scene.add(entity, generateLight(LightType::Directional));
            break;
        case EntityKind::RectLight:
            scene.add(entity, generateLight(LightType::Rect));
            break;
        case EntityKind::DiskLight:
            scene.add(entity, generateLight(LightType::Disk));
            break;
        case EntityKind::Cube:     addMesh(generateCube());     break;
        case EntityKind::Sphere:   addMesh(generateSphere());   break;
        case EntityKind::Plane:    addMesh(generatePlane());    break;
        case EntityKind::Triangle: addMesh(generateTriangle()); break;
        case EntityKind::Pyramid:  addMesh(generatePyramid());  break;
        case EntityKind::Cone:     addMesh(generateCone());     break;
        case EntityKind::Camera: {
            Camera cam;
            cam.active = false;
            scene.add(entity, cam);
            break;
        }
        case EntityKind::ReflectionProbe:
            scene.add(entity, ReflectionProbe{});
            break;
        case EntityKind::IrradianceVolume:
            scene.add(entity, IrradianceVolume{});
            break;
        case EntityKind::Decal:
            scene.add(entity, Decal{});
            break;
        case EntityKind::ParticleEmitter:
            scene.add(entity, ParticleEmitter{});
            break;
        case EntityKind::UICanvas:                          break;  // UICanvas added above
        case EntityKind::UIPanel:  scene.add(entity, UIImage{});  break;
        case EntityKind::UIText:   scene.add(entity, UIText{});   break;
        case EntityKind::UIButton: scene.add(entity, UIButton{}); break;
    }

    // A new UI element defaults to living under the selected canvas / element so
    // it renders immediately; otherwise it starts as a root for the user to
    // parent (a UI element needs a UICanvas ancestor to be laid out and drawn).
    uint32_t parentSlot = 0;
    if (isUIElement && state.selectedEntity && scene.isAlive(state.selectedEntity)
        && (scene.has<UICanvas>(state.selectedEntity) || scene.has<UIElement>(state.selectedEntity))) {
        // Unless that canvas belongs to a prefab instance. The scene stores an
        // instance as a reference and rebuilds its subtree from the file, so an
        // element parented in there is never written: it would draw until the
        // next load and then be gone. Leave it outside and say why - the same
        // answer reparentKeepingWorld gives a drag that aims there.
        if (PrefabOverrides::instanceRoot(scene, state.selectedEntity)) {
            state.pushToast(EditorState::ToastKind::Warning,
                            "A prefab instance is built from its prefab - the new element is "
                            "left outside it, and needs a canvas the scene owns");
        } else {
            HierarchyOperations::setParent(scene, entity, state.selectedEntity);
            parentSlot = state.selectedEntity.index;
        }
    }

    // Snapshot the just-created entity so undo can resurrect it intact
    // (CreateEntityCommand::undo destroys; redo re-creates at the same slot).
    // The parent slot rides along: EntitySnapshot is leaf-only, so without it a
    // redone UI element comes back as a root and stops being drawn.
    state.commands.push(std::make_unique<CreateEntityCommand>(
        EntitySnapshot::capture(scene, entity), "Create Entity", parentSlot));
    commitStructureChange(state);
    return entity;
}

namespace {
// How far along X a copy lands from its source, so it is visible rather than
// hidden inside the original.
constexpr float DUPLICATE_OFFSET_X = 1.0f;

// A copy and the step that removes it again, handed back together because the
// batch path needs that step inside the composite it pushes for the whole set.
struct Duplicate {
    EntityId                 entity;
    std::unique_ptr<Command> step;
};

// The duplicate core shared by the single and batch paths: clone @p source
// via the snapshot machinery (so "what makes up an entity" lives in one
// place), nudged off the original, never stealing active-camera/auto-play.
//
// An instance root is instanced from its prefab again instead, carrying its
// overrides over. Its interior belongs to the prefab, so copying the root's
// components alone yields an instance with nothing under it, and copying the
// whole subtree yields the loose entity block the feature exists to replace.
Duplicate duplicateOne(Scene& scene, ResourceManager& resources, EditorState& state,
                       EntityId source) {
    if (scene.has<PrefabInstance>(source)) {
        const PrefabInstance instance = scene.get<PrefabInstance>(source);

        Transform at = scene.has<Transform>(source) ? scene.get<Transform>(source) : Transform{};
        at.position.x += DUPLICATE_OFFSET_X;

        const EntityId copy = scene.createEntity();
        scene.add(copy, Transform{at});
        scene.add(copy, PrefabInstance{instance});
        if (!Prefab::instantiateInto(scene, resources, instance.source, copy,
                                     instance.overrides)) {
            // The subtree, not the root: a build that stopped partway has already
            // parented whatever it managed to create under it.
            HierarchyOperations::destroyHierarchy(scene, copy);
            const std::string name = std::filesystem::path(instance.source).filename().string();
            state.pushToast(EditorState::ToastKind::Error,
                            "Could not duplicate the instance of '" + name + "'");
            return {};
        }
        return {copy, std::make_unique<PlacePrefabCommand>(
                          resources, instance, copy, at, "Duplicate Entity")};
    }

    EntitySnapshot snap = EntitySnapshot::capture(scene, source);
    if (snap.transform) snap.transform->position.x += DUPLICATE_OFFSET_X;
    if (snap.camera)    snap.camera->active = false;
    if (snap.animation) snap.animation->playing = false;
    // A uid names an entity inside a prefab and the copy is a scene entity of
    // its own, so keeping the number would point the original's overrides at it.
    snap.prefabEntity.reset();

    const EntityId newId = scene.createEntity();
    snap.apply(scene, newId);
    return {newId, std::make_unique<CreateEntityCommand>(
                       EntitySnapshot::capture(scene, newId), "Duplicate Entity")};
}
} // namespace

void duplicateEntity(Scene& scene, ResourceManager& resources, EditorState& state,
                     EntityId source) {
    Duplicate copy = duplicateOne(scene, resources, state, source);
    if (!copy.entity) return;

    state.commands.push(std::move(copy.step));
    commitStructureChange(state);
    state.selectEntity(copy.entity);
}

void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) duplicateEntity(scene, resources, state, state.selectedEntity);
        return;
    }

    // Not filtered to selection roots the way deleteSelection is: EntitySnapshot
    // has no Hierarchy, so every clone lands unparented and flat. Skipping a
    // selected child would mean it is never duplicated at all.
    const std::vector<EntityId> sources = state.selection;
    auto batch = std::make_unique<CompositeCommand>("Duplicate Selection");
    std::vector<EntityId> clones;
    clones.reserve(sources.size());
    for (EntityId src : sources) {
        if (!scene.isAlive(src)) continue;
        Duplicate copy = duplicateOne(scene, resources, state, src);
        if (!copy.entity) continue;
        batch->add(std::move(copy.step));
        clones.push_back(copy.entity);
    }
    if (clones.empty()) return;

    state.commands.push(std::move(batch));
    commitStructureChange(state);

    // The clones become the selection (first as active, like a fresh drag).
    state.selectEntity(clones.front());
    for (size_t i = 1; i < clones.size(); ++i) state.addToSelection(clones[i]);
    state.selectedEntity = clones.front();
}

bool hasSelectedAncestor(const Scene& scene, const std::vector<EntityId>& selection, EntityId id) {
    EntityId cur = id;
    for (uint32_t depth = 0; depth < HierarchyOperations::MAX_DEPTH; ++depth) {
        if (!scene.isAlive(cur) || !scene.has<Hierarchy>(cur)) return false;
        cur = scene.get<Hierarchy>(cur).parent;
        if (!cur) return false;
        if (std::find(selection.begin(), selection.end(), cur) != selection.end()) return true;
    }
    return false;
}

namespace {
// An instance's interior comes back from the prefab on every load, so a delete
// aimed there is undone by the next one. It is still the way an entity leaves a
// prefab - delete it, then write the instance back - so the gesture stands and
// says what it needs, the same answer Add Component gives inside an instance.
void warnDeleteInsideInstance(const Scene& scene, EditorState& state, EntityId entity) {
    if (!Prefab::isInsideInstance(scene, entity)) return;
    state.pushToast(EditorState::ToastKind::Warning,
                    "This entity belongs to a prefab - it comes back on the next load "
                    "unless the instance root is saved as a prefab");
}
} // namespace

void deleteSelection(Scene& scene, EditorState& state) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) deleteEntity(scene, state, state.selectedEntity);
        return;
    }

    // Roots only: an entity whose ancestor is also selected dies with that
    // ancestor's subtree - deleting it separately would double-destroy.
    // Test against the captured copy, not state.selection: the deselect below
    // has to happen before the destroys, so by the time the loop runs the live
    // selection is empty.
    const std::vector<EntityId> sel = state.selection;

    const EntityId priorSel = state.selectedEntity;
    state.deselect();

    auto batch = std::make_unique<CompositeCommand>("Delete Selection");
    for (EntityId id : sel) {
        if (!scene.isAlive(id) || hasSelectedAncestor(scene, sel, id)) continue;
        warnDeleteInsideInstance(scene, state, id);
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        HierarchyOperations::destroyHierarchy(scene, id);
        batch->add(std::make_unique<DestroySubtreeCommand>(
            std::move(snap), priorSel, "Delete Entity"));
    }
    if (batch->empty()) return;

    state.commands.push(std::move(batch));
    commitStructureChange(state);
}

namespace {
// A name as a filename: free text, so keep only what is safe in one and fall
// back rather than compose something unopenable.
std::string prefabStem(const Scene& scene, EntityId entity) {
    std::string stem = scene.has<Name>(entity) ? scene.get<Name>(entity).value : "";
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return stem.empty() ? "prefab" : stem;
}

// First prefab file name not already on disk: `base`, then "base 2", ... A
// prefab's path is its identity - it is what every instance in every scene
// resolves - so a new one must not land on a name another already answers to.
std::string uniquePrefabStem(const std::string& base) {
    std::error_code ec;
    std::string stem = base;
    for (int n = 2; std::filesystem::exists(ProjectPaths::prefabs() / (stem + ".json"), ec); ++n) {
        stem = base + " " + std::to_string(n);
    }
    return stem;
}
} // namespace

bool saveAsPrefab(Scene& scene, const ResourceManager& resources, EditorState& state,
                  EntityId entity) {
    if (!scene.isAlive(entity)) return false;

    // A subtree inside somebody else's instance is not a file's to define, and
    // Prefab::save refuses it. Answered here, where the instance root is still
    // in reach to be named: everything the editor says about a component added
    // inside an instance sends the user to Save as Prefab, and on an entity in
    // there this is the one that writes it.
    const EntityId owner = PrefabOverrides::instanceRoot(scene, entity);
    if (owner && owner != entity) {
        char rootName[64];
        getEntityDisplayName(scene, owner, rootName, sizeof(rootName));
        state.pushToast(EditorState::ToastKind::Warning,
                        std::string("This entity belongs to a prefab - Save as Prefab on '")
                            + rootName + "', the instance root, writes it with the rest");
        return false;
    }

    // An instance saves back over the prefab it came from - that is how a prefab
    // is edited. Anything else becomes a new file, because overwriting a
    // stranger's prefab would silently re-point every instance of it at this
    // subtree. Project-relative, because the instance stores this path and a
    // scene carrying it has to name the same file on another machine; the write
    // resolves it and makes the directory.
    const bool editsItsOwn = scene.has<PrefabInstance>(entity)
                          && !scene.get<PrefabInstance>(entity).source.empty();
    const std::string path = editsItsOwn
        ? scene.get<PrefabInstance>(entity).source
        : (ProjectPaths::prefabs() / (uniquePrefabStem(prefabStem(scene, entity)) + ".json"))
              .lexically_relative(ProjectPaths::projectRoot())
              .generic_string();

    const std::string shown = std::filesystem::path(path).stem().string();
    if (!Prefab::save(scene, entity, path, resources)) {
        state.pushToast(EditorState::ToastKind::Error, "Could not save prefab '" + shown + "'");
        return false;
    }

    // The subtree is stored as a reference now, and its entities are rebuilt
    // from the file on the next load - so nothing already on the command stack
    // still describes the scene, the same reason a scene load clears it.
    state.commands.clear();
    state.markSceneDirty();
    state.pushToast(EditorState::ToastKind::Info, "Saved prefab '" + shown + "'");
    return true;
}

EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state,
                     const std::string& path) {
    const Transform at{};
    const EntityId root = Prefab::instantiate(scene, resources, path, at);
    if (!root) {
        const std::string name = std::filesystem::path(path).filename().string();
        state.pushToast(EditorState::ToastKind::Error, "Could not place prefab '" + name + "'");
        return {};
    }

    state.commands.push(std::make_unique<PlacePrefabCommand>(
        resources, scene.get<PrefabInstance>(root), root, at, "Place Prefab"));
    commitStructureChange(state);
    state.selectEntity(root);
    return root;
}

void deleteEntity(Scene& scene, EditorState& state, EntityId entity) {
    warnDeleteInsideInstance(scene, state, entity);

    const EntityId priorSel = state.selectedEntity;
    if (state.selectedEntity == entity) state.deselect();

    // SubtreeSnapshot also covers the single-entity case (nodes.size() == 1)
    // and records the original parent, so undo always restores the entity
    // under its original parent even for leaves.
    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, entity);
    const bool hadChildren = scene.has<Hierarchy>(entity)
                          && scene.get<Hierarchy>(entity).firstChild;
    const char* label = hadChildren ? "Delete Subtree" : "Delete Entity";

    HierarchyOperations::destroyHierarchy(scene, entity);
    state.commands.push(std::make_unique<DestroySubtreeCommand>(
        std::move(snap), priorSel, label));
    commitStructureChange(state);
}

void undo(Scene& scene, EditorState& state) {
    // An empty history is not an edit. The keyboard path is ungated (the Edit
    // menu is not), so without this Ctrl+Z on a freshly loaded scene would put
    // the '*' in the title and raise the save guard on quit.
    if (!state.commands.canUndo()) return;
    state.commands.undo(scene, state);
    state.markSceneDirty();
}

void redo(Scene& scene, EditorState& state) {
    if (!state.commands.canRedo()) return;
    state.commands.redo(scene, state);
    state.markSceneDirty();
}

void setActiveCamera(Scene& scene, EditorState& state, EntityId target) {
    std::vector<std::pair<uint32_t, bool>> beforeActive;
    scene.forEach<Camera>([&](EntityId other, Camera& c) {
        beforeActive.emplace_back(other.index, c.active);
        c.active = (other == target);
    });
    state.commands.push(std::make_unique<SetActiveCameraCommand>(
        target, std::move(beforeActive), "Set Main Camera"));
    state.markSceneDirty();
}

void focusOnSelected(FrameContext& ctx, EditorState& state, CameraControllerSystem& camera) {
    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) return;
    if (!ctx.scene.has<Transform>(state.selectedEntity)) return;

    bool hasParent = ctx.scene.has<Hierarchy>(state.selectedEntity)
                  && ctx.scene.get<Hierarchy>(state.selectedEntity).parent;

    glm::vec3 targetPos;
    float focusDistance = 5.0f;

    const bool selHasMesh = ctx.scene.has<Mesh>(state.selectedEntity)
        && ctx.scene.get<Mesh>(state.selectedEntity).mesh
        && ctx.resources.isAlive(ctx.scene.get<Mesh>(state.selectedEntity).mesh);
    if (selHasMesh) {
        const auto& mesh = ctx.scene.get<Mesh>(state.selectedEntity);
        const auto& asset = ctx.resources.get(mesh.mesh);

        glm::mat4 model = hasParent
            ? HierarchyOperations::computeWorldMatrix(ctx.scene, state.selectedEntity)
            : Transform::computeModelMatrix(ctx.scene.get<Transform>(state.selectedEntity));

        if (Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) {
            glm::vec3 localCenter = (asset.boundsMin + asset.boundsMax) * 0.5f;
            targetPos = glm::vec3(model * glm::vec4(localCenter, 1.0f));

            glm::vec3 extent = asset.boundsMax - asset.boundsMin;
            float maxExtent = glm::max(extent.x, glm::max(extent.y, extent.z));
            const auto& t = ctx.scene.get<Transform>(state.selectedEntity);
            float maxScale = glm::max(t.scale.x, glm::max(t.scale.y, t.scale.z));
            focusDistance = glm::max(maxExtent * maxScale * 1.5f, 2.0f);
        } else {
            targetPos = glm::vec3(model[3]);
        }
    } else {
        if (hasParent) {
            glm::mat4 wm = HierarchyOperations::computeWorldMatrix(ctx.scene, state.selectedEntity);
            targetPos = glm::vec3(wm[3]);
        } else {
            targetPos = ctx.scene.get<Transform>(state.selectedEntity).position;
        }
    }

    camera.focusOn(ctx.scene, targetPos, focusDistance);
}

void frameAll(FrameContext& ctx, CameraControllerSystem& camera) {
    if (!ctx.visibility || ctx.visibility->entries.empty()) return;

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    bool any = false;
    for (const VisibleEntity& v : ctx.visibility->entries) {
        if (!ctx.scene.has<Mesh>(v.id)) continue;
        const auto& mesh = ctx.scene.get<Mesh>(v.id);
        if (!mesh.mesh || !ctx.resources.isAlive(mesh.mesh)) continue;
        const auto& asset = ctx.resources.get(mesh.mesh);
        if (!Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) continue;

        glm::vec3 wMin, wMax;
        Math::localToWorldAABB(v.model, asset.boundsMin, asset.boundsMax, wMin, wMax);
        mn = glm::min(mn, wMin);
        mx = glm::max(mx, wMax);
        any = true;
    }
    if (!any) return;

    const glm::vec3 center = (mn + mx) * 0.5f;
    const glm::vec3 extent = mx - mn;
    const float diag = glm::length(extent);
    // Fit a sphere of radius diag/2 in the perspective frustum. A 1.1x pad on
    // the diagonal gives breathing room, with a 2.0 floor so tiny scenes don't
    // pull the camera inside the geometry. The camera controller normalises the
    // look direction.
    const float distance = std::max(2.0f, diag * 1.1f);
    camera.focusOn(ctx.scene, center, distance);
}

void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (ImGui::BeginMenu("Create")) {
        auto item = [&](EditorIcon icon, const char* label, EntityKind k) {
            if (iconMenuItem(icon, label)) state.selectEntity(createEntity(scene, resources, state, k));
        };
        item(EditorIcon::Empty, "Empty", EntityKind::Empty);
        ImGui::Separator();
        item(EditorIcon::Camera,   "Camera",            EntityKind::Camera);
        item(EditorIcon::Probe,    "Reflection Probe",  EntityKind::ReflectionProbe);
        item(EditorIcon::Volume,   "Irradiance Volume", EntityKind::IrradianceVolume);
        item(EditorIcon::Decal,    "Decal",             EntityKind::Decal);
        item(EditorIcon::Particle, "Particle Emitter",  EntityKind::ParticleEmitter);
        ImGui::Separator();
        if (ImGui::BeginMenu("Light")) {
            item(EditorIcon::LightDir,   "Directional", EntityKind::DirectionalLight);
            item(EditorIcon::LightPoint, "Point",       EntityKind::PointLight);
            item(EditorIcon::LightSpot,  "Spot",        EntityKind::SpotLight);
            item(EditorIcon::LightRect,  "Rect",        EntityKind::RectLight);
            item(EditorIcon::LightDisk,  "Disk",        EntityKind::DiskLight);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Primitive")) {
            item(EditorIcon::Cube,     "Cube",     EntityKind::Cube);
            item(EditorIcon::Sphere,   "Sphere",   EntityKind::Sphere);
            item(EditorIcon::Plane,    "Plane",    EntityKind::Plane);
            item(EditorIcon::Cone,     "Cone",     EntityKind::Cone);
            item(EditorIcon::Pyramid,  "Pyramid",  EntityKind::Pyramid);
            item(EditorIcon::Triangle, "Triangle", EntityKind::Triangle);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI")) {
            // A canvas is the screen-space root; panels/text/buttons created with
            // a canvas or element selected drop in as its children.
            item(EditorIcon::UICanvas, "Canvas", EntityKind::UICanvas);
            item(EditorIcon::UIImage,  "Panel",  EntityKind::UIPanel);
            item(EditorIcon::UIText,   "Text",   EntityKind::UIText);
            item(EditorIcon::UIButton, "Button", EntityKind::UIButton);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Neither modal can live here: the menu closes on click and this
        // function stops being called. Defer to the dialogs EditorSystem owns.
        if (iconMenuItem(EditorIcon::Duplicate, "Prefab")) state.requestPlacePrefab = true;
        if (iconMenuItem(EditorIcon::Import, "Import Model")) state.requestModelImport = true;
        ImGui::EndMenu();
    }
}

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        const std::filesystem::path appRoot = ProjectPaths::projectRoot();
        m_picker.options.popupId    = "Import Model";
        m_picker.options.title      = "Import Model";
        m_picker.options.root       = ProjectPaths::assets();
        m_picker.options.recursive  = true;
        m_picker.options.kind       = AssetPicker::Kind::Files;
        m_picker.options.extensions = {
            ".gltf", ".glb", ".obj", ".fbx", ".dae", ".stl", ".ply", ".3ds"
        };
        m_picker.options.maxResults = 2000;
        m_picker.options.relativeTo = appRoot;
        m_picker.options.hint       = "glTF / GLB / OBJ / FBX / DAE / STL / PLY / 3DS";
        m_picker.open();
        state.requestModelImport = false;
    }
    std::string picked;
    if (m_picker.draw(picked)) {
        EntityId rootId = importModelIntoScene(picked, resources, scene);
        if (rootId) {
            state.selectEntity(rootId);
            commitStructureChange(state);
        }
    }
}

namespace {
// Whether the project has a prefab to offer. A project with none is the normal
// starting state, and the picker cannot say so itself: on a missing prefabs/ it
// warns about a root it could not iterate and then shows the same empty list a
// real empty directory produces.
bool hasAnyPrefab() {
    std::error_code ec;
    // Recursive, because that is what the picker below lists: a check that only
    // looked at the top level would report "none" on a project that keeps its
    // prefabs in folders.
    for (const auto& entry :
            std::filesystem::recursive_directory_iterator(ProjectPaths::prefabs(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") return true;
    }
    return false;
}
} // namespace

void PlacePrefabDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestPlacePrefab) {
        state.requestPlacePrefab = false;
        if (hasAnyPrefab()) {
            m_picker.options.popupId    = "Place Prefab";
            m_picker.options.title      = "Place Prefab";
            m_picker.options.root       = ProjectPaths::prefabs();
            m_picker.options.recursive  = true;
            m_picker.options.kind       = AssetPicker::Kind::Files;
            m_picker.options.extensions = {".json"};
            // Project-relative, because that is what the instance stores and
            // what a scene carrying it has to resolve on another machine.
            m_picker.options.relativeTo = ProjectPaths::projectRoot();
            m_picker.open();
        } else {
            state.pushToast(EditorState::ToastKind::Info,
                            "No prefabs yet - right-click an entity and Save as Prefab");
        }
    }

    std::string picked;
    if (m_picker.draw(picked)) placePrefab(scene, resources, state, picked);
}

} // namespace EditorActions
} // namespace Vkm::Engine
