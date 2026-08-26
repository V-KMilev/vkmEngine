#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/editor_actions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "logger.h"

#include "io/json_file.h"
#include "io/project_paths.h"
#include "framework/editor_commands.h"
#include "framework/editor_state.h"
#include "framework/prefab_overrides.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/physics/character_controller.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/render/particle_emitter.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
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
#include "io/project.h"
#include "io/project_paths.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"
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
    if (!scene.isAlive(child)) return;

    // A UI element carries no Transform - its canvas lays it out in screen
    // space - so there is no world pose to preserve. A step to skip, not a move
    // to refuse: an element made before its canvas needs a way back under one.
    const bool hasTransform = scene.has<Transform>(child);

    // An instance's interior belongs to its prefab, so neither move survives a
    // load and both look like they worked; see docs/reference/editor.md, "What
    // an instance will not let you do".
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
    const Transform before = hasTransform ? scene.get<Transform>(child) : Transform{};
    const glm::mat4 world  = hasTransform
        ? HierarchyOperations::computeWorldMatrix(scene, child)
        : glm::mat4(1.0f);

    const bool toParent = newParent && scene.isAlive(newParent);
    if (toParent) {
        HierarchyOperations::setParent(scene, child, newParent);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }

    Transform after = before;
    if (hasTransform) {
        // Re-express the preserved world matrix in the new parent's space so the
        // entity stays put. Unparenting to root leaves local == world.
        glm::mat4 local = world;
        if (toParent && scene.has<Transform>(newParent)) {
            local = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, newParent)) * world;
        }
        Transform& t = scene.get<Transform>(child);
        setTransformFromMatrix(t, local);
        after = t;
    }

    state.commands.push(std::make_unique<ReparentCommand>(
        child, oldParent, toParent ? newParent : EntityId{}, before, after, label));
    commitHierarchyMutation(state);

    // An element outside every canvas stops drawing, and nothing else says so.
    // The move still happens - it is a legitimate step on the way somewhere -
    // but the author hears about it while the element is still where they put it.
    if (scene.has<UIElement>(child) && !hasCanvasAncestor(scene, child)) {
        state.pushToast(EditorState::ToastKind::Warning,
                        "UI elements are drawn by the canvas above them - this one has no "
                        "canvas ancestor now, so it will not appear");
    }
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
    MaterialHandle nh = resources.add(std::move(copy),
        uniqueMaterialName(resources, src.name() + " copy"));
    if (!nh) return MaterialHandle{};

    if (assignTo) assignTo->material = nh;
    state.markSceneDirty();
    return nh;
}

MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state) {
    const MaterialHandle base = generateDefaultMaterial(resources);
    if (!base) return MaterialHandle{};

    // A copy of the default, not the default renamed: that one asset is what
    // every primitive and every cold-start load resolves "material:default" to,
    // so renaming it would take it out from under all of them.
    MaterialAsset copy = resources.get(base);
    MaterialHandle h = resources.add(std::move(copy), uniqueMaterialName(resources, "Material"));
    if (!h) return MaterialHandle{};

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
        case EntityKind::AudioSource:      return "Audio Source";
        case EntityKind::AudioListener:    return "Audio Listener";
        case EntityKind::UICanvas:         return "UI Canvas";
        case EntityKind::UIPanel:          return "UI Panel";
        case EntityKind::UIText:           return "UI Text";
        case EntityKind::UIButton:         return "UI Button";
        case EntityKind::Character:        return "Character";
        case EntityKind::StaticBody:       return "Static Body";
    }
    return "Entity";
}
} // namespace

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

    // Both halves reuse what the graph already holds under the name they would
    // have taken. A primitive is a generator call and a default material, and
    // neither is anything the author distinguished from the last one they made.
    auto addMesh = [&](MeshAsset mesh) {
        auto meshHandle = addGeneratedMesh(resources, std::move(mesh));
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
        case EntityKind::Character: {
            // The pairing every DANGER line on the CharacterController card
            // checks for: a capsule, a body that will not tip over or doze off,
            // and the controller that drives them. Made together because made
            // apart is three visits to the Add Component list and a card that
            // spends them telling the author what is still missing.
            ColliderPart part;
            part.shape = ColliderShape::Capsule;
            part.radius = 0.3f;
            part.halfHeight = 0.6f;
            part.center = {0.0f, part.halfHeight + part.radius, 0.0f};
            Collider collider;
            collider.parts = { part };
            scene.add(entity, std::move(collider));

            Rigidbody body;
            body.mass = 70.0f;
            body.freezeRotation = true;
            body.canSleep = false;
            scene.add(entity, std::move(body));

            scene.add(entity, CharacterController{});
            break;
        }
        case EntityKind::StaticBody: {
            // A blocker: geometry the world collides with and nothing moves.
            Rigidbody body;
            body.isStatic = true;
            scene.add(entity, std::move(body));
            scene.add(entity, Collider{});
            break;
        }
        case EntityKind::Cube:     addMesh(generateCube());     break;
        case EntityKind::Sphere:   addMesh(generateSphere());   break;
        case EntityKind::Plane:    addMesh(generatePlane());    break;
        case EntityKind::Triangle: addMesh(generateTriangle()); break;
        case EntityKind::Pyramid:  addMesh(generatePyramid());  break;
        case EntityKind::Cone:     addMesh(generateCone());     break;
        case EntityKind::Camera: {
            Camera cam;
            // Inactive, so a new camera cannot hijack the view from the one the
            // author works through - unless there is none, the one case where
            // creating one is the recovery and inactive looks like a no-op.
            cam.active = !findActiveCamera(scene);
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
        case EntityKind::AudioSource:
            scene.add(entity, AudioSource{});
            break;
        case EntityKind::AudioListener:
            scene.add(entity, AudioListener{});
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
        // Unless that canvas belongs to a prefab instance, whose interior is
        // never written: the element would draw until the next load and then be
        // gone. Left outside, with the answer a drag aiming there also gets.
        if (PrefabOverrides::instanceRoot(scene, state.selectedEntity)) {
            state.pushToast(EditorState::ToastKind::Warning,
                            "A prefab instance is built from its prefab - the new element is "
                            "left outside it, and needs a canvas the scene owns");
        } else {
            HierarchyOperations::setParent(scene, entity, state.selectedEntity);
            parentSlot = state.selectedEntity.slot();
        }
    }

    // Snapshot the new entity so undo can resurrect it intact. The parent slot
    // rides along because EntitySnapshot is leaf-only: without it a redone UI
    // element comes back as a root and stops being drawn.
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

    // A ragdoll's bones are entities it owns, and a snapshot copies the ids
    // verbatim. A copy that kept them would drive the original's skeleton from
    // two places and destroy it when the copy was deleted - the observer
    // destroys what the component names. The copy has no bones until someone
    // builds them, and saying so is the only honest value here.
    if (snap.ragdoll) {
        snap.ragdoll->bones.clear();
        snap.ragdoll->root = EntityId{};
        snap.ragdoll->active = false;
    }

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
    // subtree, and deleting it separately would double-destroy. Tested against
    // this copy - the deselect below empties the live selection first.
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
    // in reach to be named in the refusal.
    const EntityId owner = PrefabOverrides::instanceRoot(scene, entity);
    if (owner && owner != entity) {
        char rootName[64];
        getEntityDisplayName(scene, owner, rootName, sizeof(rootName));
        state.pushToast(EditorState::ToastKind::Warning,
                        std::string("This entity belongs to a prefab - Save as Prefab on '")
                            + rootName + "', the instance root, writes it with the rest");
        return false;
    }

    // An instance saves back over its own source, anything else takes a free name
    // (docs/reference/editor.md, "Save as Prefab"). Project-relative, because the
    // instance stores this path and a scene has to carry it across machines.
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

    // Every step addressing an entity in the subtree is dropped, and only those;
    // see docs/reference/editor.md, "Save as Prefab".
    std::vector<uint32_t> subtreeSlots{entity.slot()};
    for (size_t i = 0; i < subtreeSlots.size(); ++i) {
        HierarchyOperations::forEachChild(scene, scene.entityAt(subtreeSlots[i]),
            [&](EntityId child) {
                if (scene.isAlive(child)) subtreeSlots.push_back(child.slot());
            });
    }
    state.commands.forget(subtreeSlots);
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
        beforeActive.emplace_back(other.slot(), c.active);
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
    // Fit a sphere of radius diag/2 in the perspective frustum: a 1.1x pad for
    // breathing room, with a 2.0 floor so a tiny scene does not pull the camera
    // inside its own geometry.
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
        item(EditorIcon::Audio,    "Audio Source",      EntityKind::AudioSource);
        item(EditorIcon::Listener, "Audio Listener",    EntityKind::AudioListener);
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
        if (ImGui::BeginMenu("Physics")) {
            item(EditorIcon::Character, "Character",   EntityKind::Character);
            item(EditorIcon::Colliders, "Static Body", EntityKind::StaticBody);
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
        if (iconMenuItem(EditorIcon::Prefab, "Prefab")) state.requestPlacePrefab = true;
        if (iconMenuItem(EditorIcon::Import, "Import Model")) state.requestModelImport = true;
        ImGui::EndMenu();
    }
}

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        const std::filesystem::path appRoot = ProjectPaths::projectRoot();
        m_picker.options().popupId    = "Import Model";
        m_picker.options().title      = "Import Model";
        m_picker.options().root       = ProjectPaths::assets();
        m_picker.options().recursive  = true;
        m_picker.options().kind       = AssetPicker::Kind::Files;
        m_picker.options().extensions = {
            ".gltf", ".glb", ".obj", ".fbx", ".dae", ".stl", ".ply", ".3ds"
        };
        m_picker.options().maxResults = 2000;
        m_picker.options().relativeTo = appRoot;
        m_picker.options().hint       = "glTF / GLB / OBJ / FBX / DAE / STL / PLY / 3DS";
        m_picker.open();
        state.requestModelImport = false;
    }
    std::string picked;
    if (m_picker.draw(picked)) {
        const ModelImport imported = importModelIntoScene(picked, resources, scene);
        if (imported.root) {
            state.selectEntity(imported.root);
            commitStructureChange(state);
        } else if (imported.ok) {
            // A file of clips and no mesh: the import worked and there is
            // nothing to select, which looked exactly like failure before -
            // the picker closed and the editor said nothing at all.
            state.pushToast(EditorState::ToastKind::Info,
                            "Imported " + std::to_string(imported.clips)
                                + " clip(s). The file has no mesh, so nothing "
                                  "was added to the scene.");
        } else {
            state.pushToast(EditorState::ToastKind::Error,
                            "Could not import '" + picked + "'");
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
            m_picker.options().popupId    = "Place Prefab";
            m_picker.options().title      = "Place Prefab";
            m_picker.options().root       = ProjectPaths::prefabs();
            m_picker.options().recursive  = true;
            m_picker.options().kind       = AssetPicker::Kind::Files;
            m_picker.options().extensions = {".json"};
            // Project-relative, because that is what the instance stores and
            // what a scene carrying it has to resolve on another machine.
            m_picker.options().relativeTo = ProjectPaths::projectRoot();
            m_picker.open();
        } else {
            state.pushToast(EditorState::ToastKind::Info,
                            "No prefabs yet - right-click an entity and Save as Prefab");
        }
    }

    std::string picked;
    if (m_picker.draw(picked)) placePrefab(scene, resources, state, picked);
}

bool NewProjectDialog::create(const std::filesystem::path& dest, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        error = "That directory already exists and is not empty";
        return false;
    }

    const fs::path template_ = ProjectPaths::engineRoot() / "templates" / "default";
    if (!fs::is_directory(template_, ec)) {
        error = "No project template shipped with this engine";
        return false;
    }

    fs::copy(template_, dest, fs::copy_options::recursive, ec);
    if (ec) {
        error = "Could not copy the template: " + ec.message();
        return false;
    }

    // Named after the directory, so the author's first act is not editing JSON,
    // and stamped with the engine that answered - the host compares that string
    // against its own, and a literal left here would warn on every open.
    const fs::path projectFile = dest / "project.json";
    nlohmann::json doc;
    if (!detail::readJsonFile(projectFile, doc, "project")) {
        error = "The template's project.json could not be read";
        return false;
    }
    doc["name"]          = dest.filename().string();
    doc["engineVersion"] = APP_VERSION;
    if (!detail::writeJsonFile(projectFile, doc, "project")) {
        error = "Could not write project.json";
        return false;
    }

    // find_package asks by MAJOR.MINOR, so the patch digit is dropped here and
    // kept above - the same split the vkm CLI makes for the same reason.
    const fs::path cml = dest / "CMakeLists.txt";
    std::ifstream in(cml);
    if (!in) {
        error = "The template's CMakeLists.txt could not be read";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    const std::string full = APP_VERSION;
    const std::size_t secondDot = full.find('.', full.find('.') + 1);
    const std::string majorMinor = secondDot == std::string::npos ? full : full.substr(0, secondDot);
    const std::string token = "@VKM_ENGINE_VERSION@";
    for (std::size_t at = text.find(token); at != std::string::npos;
         at = text.find(token, at + majorMinor.size())) {
        text.replace(at, token.size(), majorMinor);
    }

    std::ofstream out(cml, std::ios::trunc);
    if (!out) {
        error = "Could not write the project's CMakeLists.txt";
        return false;
    }
    out << text;
    return true;
}

void NewProjectDialog::draw(EditorState& state) {
    if (state.requestNewProject) {
        state.requestNewProject = false;
        m_open  = true;
        m_error.clear();
        if (m_parentBuffer[0] == '\0') {
            const std::string parent = ProjectPaths::projectRoot().parent_path().string();
            std::snprintf(m_parentBuffer, sizeof(m_parentBuffer), "%s", parent.c_str());
        }
    }
    if (!beginDialog("New Project", m_open)) return;

    ImGui::TextDisabled("A project is a directory: its scenes, its assets, and the code that plays them.");
    ImGui::Spacing();

    ImGui::TextDisabled("Name");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    const bool entered = ImGui::InputText("##NewProjectName", m_nameBuffer, sizeof(m_nameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::TextDisabled("In");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    ImGui::InputText("##NewProjectParent", m_parentBuffer, sizeof(m_parentBuffer));

    const std::string name   = m_nameBuffer;
    const std::string parent = m_parentBuffer;
    const bool named = !name.empty() && name.find_first_of("/\\") == std::string::npos;
    const std::filesystem::path dest = named && !parent.empty()
        ? std::filesystem::path(parent) / name : std::filesystem::path{};

    if (!dest.empty()) ImGui::TextDisabled("%s", dest.string().c_str());

    const DialogResult r = dialogButtons(m_open, "Create", named && !parent.empty(), entered);
    if (r == DialogResult::Confirm) {
        m_error.clear();
        if (create(dest, m_error)) {
            // Asked for rather than opened here: a new project replaces the
            // scene in the world, which is the guard's business, not a dialog's.
            state.requestSceneAction(EditorState::SceneAction::OpenProject, dest.string());
            m_nameBuffer[0] = '\0';
            m_open = false;
            ImGui::CloseCurrentPopup();
        } else {
            // Said out here, because dialogButtons has already closed the popup
            // by the time this runs - a message written inside it would be drawn
            // to a dialog that is gone, which is how the likeliest failure of
            // all, a name already taken, reported itself as success.
            state.pushToast(EditorState::ToastKind::Error, m_error);
            LOG_ERROR("New Project: %s", m_error.c_str());
        }
    }
    endDialog();
}

void OpenProjectDialog::draw(EditorState& state) {
    if (state.requestOpenProject) {
        state.requestOpenProject = false;
        m_open = true;
    }
    if (!beginDialog("Open Project", m_open)) return;

    ImGui::TextDisabled("A project is a directory with a project.json in it.");
    ImGui::Spacing();

    // Recents first: switching between a few projects is the common case, and
    // typing a path for it every time would be the wrong default.
    std::string chosen;
    if (!state.recentProjects.empty()) {
        ImGui::TextDisabled("Recent");
        for (const std::string& path : state.recentProjects) {
            ImGui::PushID(path.c_str());
            const std::string label = std::filesystem::path(path).filename().string();
            if (ImGui::Selectable(label.empty() ? path.c_str() : label.c_str())) {
                chosen = path;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::TextDisabled("Path");
    ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
    const bool entered = ImGui::InputText("##ProjectPath", m_pathBuffer, sizeof(m_pathBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue);

    const std::string typed = m_pathBuffer;
    const bool typedIsProject = !typed.empty() && !findProjectRoot(typed).empty();
    if (!typed.empty() && !typedIsProject) {
        ImGui::TextColored(EditorStyle::WARNING, "No project.json here");
    }

    const DialogResult r = dialogButtons(m_open, "Open", typedIsProject, entered);
    if (r == DialogResult::Confirm) chosen = typed;

    if (!chosen.empty()) {
        m_open = false;
        ImGui::CloseCurrentPopup();
    }

    endDialog();

    if (!chosen.empty()) {
        state.requestSceneAction(EditorState::SceneAction::OpenProject, chosen);
        m_pathBuffer[0] = '\0';
    }
}

} // namespace EditorActions
} // namespace Vkm::Engine
