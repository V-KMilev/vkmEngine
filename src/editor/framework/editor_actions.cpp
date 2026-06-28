#include "framework/editor_actions.h"

#include <filesystem>
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

namespace Engine {
namespace EditorActions {

void commitHierarchyMutation(Scene& scene, EditorState& state, EntityId entity) {
    HierarchyOperations::markDirty(scene, entity);
    state.hierarchyDirty = true;
    state.markSceneDirty();
}

namespace {
// Write a model matrix back into a local Transform (TRS), matching the gizmo's
// decomposition: translation from the last column, per-axis scale from the
// basis-column lengths, rotation from the scale-normalised basis.
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
    }
    return "Entity";
}
}

EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityKind kind) {
    auto entity = scene.createEntity();
    EntityId id = entity.getID();

    scene.add(entity, Transform{});
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
            scene.add(entity, generatePointLight());
            break;
        case EntityKind::SpotLight:
            scene.add(entity, generateSpotLight());
            break;
        case EntityKind::DirectionalLight:
            scene.add(entity, generateDirectionalLight());
            break;
        case EntityKind::RectLight:
            scene.add(entity, generateRectLight());
            break;
        case EntityKind::DiskLight:
            scene.add(entity, generateDiskLight());
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
    }

    // Snapshot the just-created entity so undo can resurrect it intact
    // (CreateEntityCommand::undo destroys; redo re-creates at the same slot).
    state.commands.push(std::make_unique<CreateEntityCommand>(
        EntitySnapshot::capture(scene, id), "Create Entity"));
    commitStructureChange(state);
    return id;
}

void duplicateEntity(Scene& scene, EditorState& state, EntityId source) {
    // Reuse the snapshot machinery so the "what components make up an entity"
    // list lives in exactly one place (VKM_EDITOR_SNAPSHOT_COMPONENTS). Then
    // apply the few duplicate-specific tweaks on the captured copy: nudge it off
    // the source, and don't let the copy steal "active camera" or auto-play.
    EntitySnapshot snap = EntitySnapshot::capture(scene, source);
    if (snap.transform) snap.transform->position += glm::vec3(1.0f, 0.0f, 0.0f);
    if (snap.camera)    snap.camera->active = false;
    if (snap.animation) snap.animation->playing = false;

    Entity entity = scene.createEntity();
    EntityId newId = entity.getID();
    snap.apply(scene, newId);

    // Same path as createEntity: snapshot the result so undo destroys it.
    state.commands.push(std::make_unique<CreateEntityCommand>(
        EntitySnapshot::capture(scene, newId), "Duplicate Entity"));
    commitStructureChange(state);
    state.selectEntity(newId);
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
    state.commands.undo(scene, state);
    state.markSceneDirty();
}

void redo(Scene& scene, EditorState& state) {
    state.commands.redo(scene, state);
    state.markSceneDirty();
}

void setActiveCamera(Scene& scene, EditorState& state, EntityId target, const char* label) {
    std::vector<std::pair<uint32_t, bool>> beforeActive;
    scene.forEach<Camera>([&](EntityId other, Camera& c) {
        beforeActive.emplace_back(other.index, c.active);
        c.active = (other == target);
    });
    state.commands.push(std::make_unique<SetActiveCameraCommand>(
        target, std::move(beforeActive), label));
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
        auto item = [&](const char* label, EntityKind k) {
            if (ImGui::MenuItem(label)) state.selectEntity(createEntity(scene, resources, state, k));
        };
        item("Empty Entity", EntityKind::Empty);
        ImGui::Separator();
        item("Camera", EntityKind::Camera);
        item("Reflection Probe", EntityKind::ReflectionProbe);
        ImGui::Separator();
        item("Rect Light",        EntityKind::RectLight);
        item("Disk Light",        EntityKind::DiskLight);
        item("Spot Light",        EntityKind::SpotLight);
        item("Point Light",       EntityKind::PointLight);
        item("Directional Light", EntityKind::DirectionalLight);
        ImGui::Separator();
        item("Triangle", EntityKind::Triangle);
        item("Plane",    EntityKind::Plane);
        item("Cube",     EntityKind::Cube);
        item("Sphere",   EntityKind::Sphere);
        item("Pyramid",  EntityKind::Pyramid);
        item("Cone",     EntityKind::Cone);
        ImGui::Separator();
        // The modal can't live here: the menu closes on click and this
        // function stops being called. Defer to drawModelImportDialog().
        if (ImGui::MenuItem("Import Model")) state.requestModelImport = true;
        ImGui::EndMenu();
    }
}

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        const std::filesystem::path appRoot = ProjectPaths::root();
        m_picker.options.popupId    = "Import Model";
        m_picker.options.title      = "Import Model";
        m_picker.options.root       = appRoot / "assets";
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
            (ProjectPaths::root() / picked).string(),
            resources, scene);
        if (rootId) {
            state.selectEntity(rootId);
            commitStructureChange(state);
        }
    }
}

} // namespace EditorActions
} // namespace Engine
