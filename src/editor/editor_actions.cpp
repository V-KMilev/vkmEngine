#include "editor_actions.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>
#include <glm/glm.hpp>

#include "io/project_paths.h"
#include "command/editor_commands.h"
#include "editor_state.h"
#include "command/prefab_overrides.h"
#include "ecs/scene.h"
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
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/generate/light_generators.h"
#include "resource/generate/mesh_generators.h"
#include "resource/generate/material_generators.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {
namespace EditorActions {

void reparentKeepingWorld(
    Scene& scene,
    EditorState& state,
    EntityId child,
    EntityId newParent,
    const char* label
) {
    if (!scene.isAlive(child) || newParent == child) return;

    // Refused here although setParent declines a cycle too: the rest would still
    // rebase and push a step for a move that never happened.
    if (HierarchyOperations::isAncestorOf(scene, child, newParent)) {
        state.pushToast(ToastKind::Warning, "An entity cannot be moved under one of its own descendants");
        return;
    }

    Transform* local = scene.tryGet<Transform>(child);
    const bool hasTransform = local != nullptr;

    // An instance's interior belongs to its prefab, so neither move survives a
    // load; see docs/reference/editor.md, "What an instance will not let you do".
    if (PrefabOverrides::instanceRoot(scene, newParent)) {
        const char* message =
            "A prefab instance is built from its prefab - an entity "
            "moved into one is not saved with the scene";
        state.pushToast(ToastKind::Warning, message);
        return;
    }
    const EntityId owningInstance = PrefabOverrides::instanceRoot(scene, child);
    if (owningInstance && owningInstance != child) {
        const char* message =
            "This entity belongs to a prefab - move the instance root, or change the prefab and save it";
        state.pushToast(ToastKind::Warning, message);
        return;
    }

    EntityId oldParent{};
    if (const Hierarchy* node = scene.tryGet<Hierarchy>(child)) oldParent = node->parent;

    // The world matrix is what stays fixed across the move; `before` is for undo.
    const Transform before = hasTransform ? *local : Transform{};
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
        // Unparenting to the root leaves local == world.
        glm::mat4 rebased = world;
        if (toParent && scene.has<Transform>(newParent)) {
            rebased = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, newParent)) * world;
        }
        *local = Transform::fromModelMatrix(rebased);
        after = *local;
    }

    state.pushStep(
        std::make_unique<ReparentCommand>(
            child,
            oldParent,
            toParent ? newParent : EntityId{},
            before,
            after,
            label
        )
    );

    if (scene.has<UIElement>(child) && !hasCanvasAncestor(scene, child)) {
        const char* message =
            "UI elements are drawn by the canvas above them - this one has "
            "no canvas ancestor now, so it will not appear";
        state.pushToast(ToastKind::Warning, message);
    }
}

namespace {
// First material name not already taken: `base`, then "base 2", "base 3", ...
std::string uniqueMaterialName(ResourceManager& resources, const std::string& base) {
    std::string name = base;
    for (int n = 2; resources.findByName<MaterialAsset>(name); ++n) {
        name = base + " " + std::to_string(n);
    }
    return name;
}
} // namespace

MaterialHandle duplicateMaterial(ResourceManager& resources, MaterialHandle source) {
    if (!source) return MaterialHandle{};
    const MaterialAsset& src = resources.get(source);

    MaterialAsset copy = src;  // value copy of params + texture refs
    return resources.add(std::move(copy), uniqueMaterialName(resources, src.name() + " copy"));
}

MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state) {
    const MaterialHandle base = generateDefaultMaterial(resources);
    if (!base) return MaterialHandle{};

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
        case EntityKind::UIScrollView:     return "UI Scroll View";
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

    const bool isCanvas    = kind == EntityKind::UICanvas;
    const bool isUIElement = kind == EntityKind::UIPanel || kind == EntityKind::UIText
        || kind == EntityKind::UIButton || kind == EntityKind::UIScrollView;
    if (isCanvas)         scene.add(entity, UICanvas{});
    else if (isUIElement) scene.add(entity, UIElement{});
    else                  scene.add(entity, Transform{});

    scene.add(entity, makeName(defaultName(kind)));

    // Both reuse what the graph already holds under the name they would take.
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
            // What a CharacterController needs: a capsule and a body that will not
            // tip over or doze off.
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
            body.motion = RigidbodyMotion::Static;
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
            // Inactive unless there is no active camera, so it cannot take the game's view.
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
        case EntityKind::UIScrollView:
            // A backing panel too: a scroll view with nothing drawn is an invisible hole.
            scene.add(entity, UIImage{});
            scene.add(entity, UIScroll{});
            break;
        case EntityKind::UIText:   scene.add(entity, UIText{});   break;
        case EntityKind::UIButton: scene.add(entity, UIButton{}); break;
    }

    // A new UI element goes under the selected canvas or element so it renders at
    // once: it needs a UICanvas ancestor to be laid out and drawn.
    uint32_t parentSlot = 0;
    if (isUIElement && state.selectedEntity && scene.isAlive(state.selectedEntity)
        && (scene.has<UICanvas>(state.selectedEntity) || scene.has<UIElement>(state.selectedEntity))) {
        // Unless that belongs to a prefab instance, whose interior is never
        // written: the element would be gone after the next load.
        if (PrefabOverrides::instanceRoot(scene, state.selectedEntity)) {
            const char* message =
                "A prefab instance is built from its prefab - the new element is "
                "left outside it, and needs a canvas the scene owns";
            state.pushToast(ToastKind::Warning, message);
        } else {
            HierarchyOperations::setParent(scene, entity, state.selectedEntity);
            parentSlot = state.selectedEntity.slot();
        }
    }

    // The parent slot rides along because EntitySnapshot is leaf-only: without it
    // a redone UI element comes back as a root and stops being drawn.
    state.pushStep(
        std::make_unique<CreateEntityCommand>(
            EntitySnapshot::capture(scene, entity),
            "Create Entity",
            parentSlot
        )
    );
    return entity;
}

namespace {
constexpr float DUPLICATE_OFFSET_X = 1.0f;

// A copy and the step that removes it, together because the batch path puts the
// step in its composite.
struct Duplicate {
    EntityId                 entity;
    std::unique_ptr<Command> step;
};

/**
 * @brief Which nodes of @p snap belong to a prefab instance nested inside it.
 *
 * A PrefabEntity uid inherited from the instance the original sat in must go, or
 * that instance's overrides would patch the clone; one under an instance inside
 * the copy stays, since that instance's overrides address it. Relies on nodes
 * listing each parent before its children.
 *
 * @param snap The subtree being copied, root first.
 * @return One flag per node, true for a node under a nested instance root.
 */
std::vector<bool> nodesInsideNestedInstance(const SubtreeSnapshot& snap) {
    std::vector<bool> nested(snap.nodes.size(), false);
    for (size_t i = 1; i < snap.nodes.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (snap.nodes[j].snap.slotIndex != snap.nodes[i].parentSlot) continue;
            nested[i] = nested[j] || snap.nodes[j].snap.prefabInstance.has_value();
            break;
        }
    }
    return nested;
}

// duplicateEntity documents what a copy is.
Duplicate duplicateOne(Scene& scene, ResourceManager& resources, EditorState& state, EntityId source) {
    if (const PrefabInstance* held = scene.tryGet<PrefabInstance>(source)) {
        const PrefabInstance instance = *held;

        const Transform* from = scene.tryGet<Transform>(source);
        Transform at = from ? *from : Transform{};
        at.position.x += DUPLICATE_OFFSET_X;

        const EntityId copy = scene.createEntity();
        scene.add(copy, Transform{at});
        scene.add(copy, PrefabInstance{instance});
        const bool built = Prefab::instantiateInto(
            scene,
            resources,
            instance.source,
            copy,
            instance.overrides
        );
        if (!built) {
            // The subtree: a build that stopped partway has parented what it made.
            HierarchyOperations::destroyHierarchy(scene, copy);
            const std::string name = std::filesystem::path(instance.source).filename().string();
            state.pushToast(ToastKind::Error, "Could not duplicate the instance of '" + name + "'");
            return {};
        }
        // Beside the source, under its parent, since the pose is local to it.
        const Hierarchy* node   = scene.tryGet<Hierarchy>(source);
        const EntityId   parent = node ? node->parent : EntityId{};
        if (parent) HierarchyOperations::setParent(scene, copy, parent);
        auto placed = std::make_unique<PlacePrefabCommand>(
            resources,
            instance,
            copy,
            at,
            "Duplicate Entity",
            parent.slot()
        );
        return {copy, std::move(placed)};
    }

    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, source);
    if (snap.nodes.empty()) return {};

    // The root alone: its descendants are placed relative to it.
    if (snap.nodes.front().snap.transform) {
        snap.nodes.front().snap.transform->position.x += DUPLICATE_OFFSET_X;
    }

    const std::vector<bool> nested = nodesInsideNestedInstance(snap);
    for (size_t i = 0; i < snap.nodes.size(); ++i) {
        EntitySnapshot& node = snap.nodes[i].snap;
        if (node.camera)    node.camera->active = false;
        if (node.animation) node.animation->playing = false;
        // Its bones are cloned and re-stamped by apply, so it can still be thrown.
        if (node.ragdoll)   node.ragdoll->active = false;
        if (!nested[i])     node.prefabEntity.reset();
    }

    const EntityId copy = snap.apply(scene, SnapshotSlots::Fresh);
    if (!copy) return {};
    SubtreeSnapshot placed = SubtreeSnapshot::capture(scene, copy);
    return {copy, std::make_unique<CreateSubtreeCommand>(std::move(placed), "Duplicate Entity")};
}
} // namespace

void duplicateEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityId source) {
    Duplicate copy = duplicateOne(scene, resources, state, source);
    if (!copy.entity) return;

    state.pushStep(std::move(copy.step));
    state.selectEntity(copy.entity);
}

void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) duplicateEntity(scene, resources, state, state.selectedEntity);
        return;
    }

    const std::vector<EntityId> sources = state.selection;
    auto batch = std::make_unique<CompositeCommand>("Duplicate Selection");
    std::vector<EntityId> clones;
    clones.reserve(sources.size());
    for (EntityId src : sources) {
        if (!scene.isAlive(src) || hasSelectedAncestor(scene, sources, src)) continue;
        Duplicate copy = duplicateOne(scene, resources, state, src);
        if (!copy.entity) continue;
        batch->add(std::move(copy.step));
        clones.push_back(copy.entity);
    }
    if (clones.empty()) return;

    state.pushStep(std::move(batch));

    state.selectEntity(clones.front());
    for (size_t i = 1; i < clones.size(); ++i) state.addToSelection(clones[i]);
    state.makeActive(clones.front());
}

bool hasSelectedAncestor(const Scene& scene, const std::vector<EntityId>& selection, EntityId id) {
    bool found = false;
    HierarchyOperations::forSelfAndAncestors(scene, id, [&](EntityId at) {
        found = at != id && std::find(selection.begin(), selection.end(), at) != selection.end();
        return !found;
    });
    return found;
}

namespace {
// An instance's interior comes back on every load, so this delete is undone by
// the next unless the instance is written back - a warning, not a refusal.
void warnDeleteInsideInstance(const Scene& scene, EditorState& state, EntityId entity) {
    if (!Prefab::isInsideInstance(scene, entity)) return;
    const char* message =
        "This entity belongs to a prefab - it comes back on the next "
        "load unless the instance root is saved as a prefab";
    state.pushToast(ToastKind::Warning, message);
}
} // namespace

void deleteSelection(Scene& scene, EditorState& state) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) deleteEntity(scene, state, state.selectedEntity);
        return;
    }

    // A copy: the deselect below empties the live selection.
    const std::vector<EntityId> sel = state.selection;

    const EntityId priorSel = state.selectedEntity;
    state.deselect();

    auto batch = std::make_unique<CompositeCommand>("Delete Selection");
    for (EntityId id : sel) {
        if (!scene.isAlive(id) || hasSelectedAncestor(scene, sel, id)) continue;
        warnDeleteInsideInstance(scene, state, id);
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        HierarchyOperations::destroyHierarchy(scene, id);
        batch->add(std::make_unique<DestroySubtreeCommand>(std::move(snap), priorSel, "Delete Entity"));
    }
    if (batch->empty()) return;

    state.pushStep(std::move(batch));
}

namespace {
// A name as a filename: only safe characters, and a fallback over an unopenable one.
std::string prefabStem(const Scene& scene, EntityId entity) {
    const Name* named = scene.tryGet<Name>(entity);
    std::string stem = named ? named->value : "";
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return stem.empty() ? "prefab" : stem;
}

// First prefab file name not already on disk: `base`, then "base 2", ...
std::string uniquePrefabStem(const std::string& base) {
    std::error_code ec;
    std::string stem = base;
    for (int n = 2; std::filesystem::exists(ProjectPaths::prefabs() / (stem + ".json"), ec); ++n) {
        stem = base + " " + std::to_string(n);
    }
    return stem;
}
} // namespace

bool saveAsPrefab(Scene& scene, const ResourceManager& resources, EditorState& state, EntityId entity) {
    if (!scene.isAlive(entity)) return false;

    // Prefab::save refuses this too; here the instance root can be named.
    const EntityId owner = PrefabOverrides::instanceRoot(scene, entity);
    if (owner && owner != entity) {
        char rootName[64];
        getEntityDisplayName(scene, owner, rootName, sizeof(rootName));
        const std::string message = std::string("This entity belongs to a prefab - Save as Prefab on '")
            + rootName + "', the instance root, writes it with the rest";
        state.pushToast(ToastKind::Warning, message);
        return false;
    }

    // An instance saves over its own source, anything else takes a free name.
    // Project-relative: the instance stores it, and scenes move between machines.
    const PrefabInstance* instance = scene.tryGet<PrefabInstance>(entity);
    const bool editsItsOwn = instance && !instance->source.empty();
    const std::string path = editsItsOwn
        ? instance->source
        : (ProjectPaths::prefabs() / (uniquePrefabStem(prefabStem(scene, entity)) + ".json"))
            .lexically_relative(ProjectPaths::projectRoot())
            .generic_string();

    const std::string shown = std::filesystem::path(path).stem().string();
    if (!Prefab::save(scene, entity, path, resources)) {
        state.pushToast(ToastKind::Error, "Could not save prefab '" + shown + "'");
        return false;
    }

    // The walk Prefab::save wrote with, so no undo step addresses a component the
    // scene stopped storing; see docs/reference/editor.md, "Save as Prefab".
    std::vector<uint32_t> subtreeSlots;
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, entity)) {
        subtreeSlots.push_back(id.slot());
    }
    state.commands.forget(subtreeSlots);
    state.markSceneDirty();
    state.pushToast(ToastKind::Info, "Saved prefab '" + shown + "'");
    return true;
}

EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& path) {
    const Transform at{};
    const EntityId root = Prefab::instantiate(scene, resources, path, at);
    if (!root) {
        const std::string name = std::filesystem::path(path).filename().string();
        state.pushToast(ToastKind::Error, "Could not place prefab '" + name + "'");
        return {};
    }

    state.pushStep(
        std::make_unique<PlacePrefabCommand>(
            resources,
            scene.get<PrefabInstance>(root),
            root,
            at,
            "Place Prefab"
        )
    );
    state.selectEntity(root);
    return root;
}

void deleteEntity(Scene& scene, EditorState& state, EntityId entity) {
    warnDeleteInsideInstance(scene, state, entity);

    const EntityId priorSel = state.selectedEntity;
    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, entity);
    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    const bool hadChildren = node && node->firstChild;
    const char* label = hadChildren ? "Delete Subtree" : "Delete Entity";

    HierarchyOperations::destroyHierarchy(scene, entity);
    state.pushStep(std::make_unique<DestroySubtreeCommand>(std::move(snap), priorSel, label));
}

void undo(Scene& scene, EditorState& state) {
    // An empty history is not an edit: Ctrl+Z on a fresh scene must not mark it unsaved.
    if (!state.commands.canUndo()) return;
    state.commands.undo(scene, state);
    state.markSceneDirty();
}

void redo(Scene& scene, EditorState& state) {
    if (!state.commands.canRedo()) return;
    state.commands.redo(scene, state);
    state.markSceneDirty();
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
            item(EditorIcon::UICanvas, "Canvas", EntityKind::UICanvas);
            item(EditorIcon::UIImage,  "Panel",  EntityKind::UIPanel);
            item(EditorIcon::UIScroll, "Scroll View", EntityKind::UIScrollView);
            item(EditorIcon::UIText,   "Text",   EntityKind::UIText);
            item(EditorIcon::UIButton, "Button", EntityKind::UIButton);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // The menu closes on click, so the modals live in dialogs EditorSystem owns.
        if (iconMenuItem(EditorIcon::Prefab, "Prefab")) state.requestPlacePrefab = true;
        if (iconMenuItem(EditorIcon::Import, "Import Model")) state.requestModelImport = true;
        ImGui::EndMenu();
    }
}

} // namespace EditorActions
} // namespace Vkm::Engine
