#include "framework/editor_actions.h"
#include <cctype>
#include "io/scene/prefab.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>

#include <imgui.h>
#include <glm/glm.hpp>

#include "framework/editor_commands.h"
#include "framework/editor_state.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/irradiance_volume.h"
#include "ecs/component/decal.h"
#include "ecs/component/particle_emitter.h"
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

namespace Engine {
namespace EditorActions {

void commitHierarchyMutation(Scene& scene, EditorState& state, EntityId entity) {
    HierarchyOperations::markDirty(scene, entity);
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

    EntityId oldParent{};
    if (scene.has<Hierarchy>(child)) oldParent = scene.get<Hierarchy>(child).parent;

    // Capture the state to preserve: the world matrix (fixed across the move)
    // and the current local transform (for undo).
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
    commitHierarchyMutation(scene, state, child);
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
        HierarchyOperations::setParent(scene, entity, state.selectedEntity);
        parentSlot = state.selectedEntity.index;
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
// The duplicate core shared by the single and batch paths: clone @p source
// via the snapshot machinery (so "what makes up an entity" lives in one
// place), nudged off the original, never stealing active-camera/auto-play.
EntityId duplicateOne(Scene& scene, EntityId source) {
    EntitySnapshot snap = EntitySnapshot::capture(scene, source);
    if (snap.transform) snap.transform->position += glm::vec3(1.0f, 0.0f, 0.0f);
    if (snap.camera)    snap.camera->active = false;
    if (snap.animation) snap.animation->playing = false;

    const EntityId newId = scene.createEntity();
    snap.apply(scene, newId);
    return newId;
}
} // namespace

void duplicateEntity(Scene& scene, EditorState& state, EntityId source) {
    const EntityId newId = duplicateOne(scene, source);
    // Same path as createEntity: snapshot the result so undo destroys it.
    state.commands.push(std::make_unique<CreateEntityCommand>(
        EntitySnapshot::capture(scene, newId), "Duplicate Entity"));
    commitStructureChange(state);
    state.selectEntity(newId);
}

void duplicateSelection(Scene& scene, EditorState& state) {
    if (state.selection.size() <= 1) {
        if (state.selectedEntity) duplicateEntity(scene, state, state.selectedEntity);
        return;
    }

    const std::vector<EntityId> sources = state.selection;
    auto batch = std::make_unique<CompositeCommand>("Duplicate Selection");
    std::vector<EntityId> clones;
    clones.reserve(sources.size());
    for (EntityId src : sources) {
        if (!scene.isAlive(src)) continue;
        const EntityId newId = duplicateOne(scene, src);
        batch->add(std::make_unique<CreateEntityCommand>(
            EntitySnapshot::capture(scene, newId), "Duplicate Entity"));
        clones.push_back(newId);
    }
    if (clones.empty()) return;

    state.commands.push(std::move(batch));
    commitStructureChange(state);

    // The clones become the selection (first as active, like a fresh drag).
    state.selectEntity(clones.front());
    for (size_t i = 1; i < clones.size(); ++i) state.addToSelection(clones[i]);
    state.selectedEntity = clones.front();
}

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
    auto hasSelectedAncestor = [&](EntityId id) {
        EntityId cur = id;
        while (scene.isAlive(cur) && scene.has<Hierarchy>(cur)) {
            cur = scene.get<Hierarchy>(cur).parent;
            if (!cur) break;
            if (std::find(sel.begin(), sel.end(), cur) != sel.end()) return true;
        }
        return false;
    };

    const EntityId priorSel = state.selectedEntity;
    state.deselect();

    auto batch = std::make_unique<CompositeCommand>("Delete Selection");
    for (EntityId id : sel) {
        if (!scene.isAlive(id) || hasSelectedAncestor(id)) continue;
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        HierarchyOperations::destroyHierarchy(scene, id);
        batch->add(std::make_unique<DestroySubtreeCommand>(
            std::move(snap), priorSel, "Delete Entity"));
    }
    if (batch->empty()) return;

    state.commands.push(std::move(batch));
    commitStructureChange(state);
}

bool saveAsPrefab(Scene& scene, const ResourceManager& resources, EditorState& state,
                  EntityId entity) {
    if (!scene.isAlive(entity)) return false;

    // Named after the entity, so saving it again updates the same prefab. A name
    // is free text, so keep only what is safe in a filename and fall back rather
    // than write something unopenable.
    std::string stem = scene.has<Name>(entity) ? scene.get<Name>(entity).value : "";
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    if (stem.empty()) stem = "prefab";

    // Project-relative, because the instance stores this path and a scene
    // carrying it has to name the same file on another machine. The write
    // resolves it and makes the directory.
    const std::string path = (ProjectPaths::prefabs() / (stem + ".json"))
                                 .lexically_relative(ProjectPaths::projectRoot())
                                 .generic_string();

    if (!Prefab::save(scene, entity, path, resources)) {
        state.pushToast(EditorState::ToastKind::Error, "Could not save prefab '" + stem + "'");
        return false;
    }

    // The subtree is now stored as a reference, which changes what the scene
    // writes for it.
    state.sceneDirty = true;
    state.pushToast(EditorState::ToastKind::Info, "Saved prefab '" + stem + "'");
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
        resources, path, root, at, "Place Prefab"));
    commitStructureChange(state);
    state.selectEntity(root);
    return root;
}

void deleteEntity(Scene& scene, EditorState& state, EntityId entity) {
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
        EntityId rootId = importModelIntoScene(
            (ProjectPaths::projectRoot() / picked).string(),
            resources, scene);
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
} // namespace Engine
