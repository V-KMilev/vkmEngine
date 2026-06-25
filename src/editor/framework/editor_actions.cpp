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
#include "system/script/script_component.h"
#include "resource/resource_manager.h"
#include "resource/asset/material_asset.h"
#include "system/visibility/visibility.h"
#include "system/visibility/bounds_utils.h"
#include "system/camera/camera_controller.h"
#include "generator/light_generators.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"
#include "loader/model_loader.h"
#include "io/project_paths.h"

namespace Engine {
namespace EditorActions {

void commitHierarchyMutation(Scene& scene, EditorState& state, EntityId entity) {
    HierarchyOperations::markDirty(scene, entity);
    state.hierarchyDirty = true;
    state.markSceneDirty();
}

void commitStructureChange(EditorState& state) {
    state.hierarchyDirty = true;
    state.markSceneDirty();
}

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

    // Unique name: "<src> copy", then "<src> copy 2", ... so the duplicate is a
    // distinct, separately-referenceable asset (the name is the identity).
    const std::string base = (src.name.empty() ? std::string("material") : src.name) + " copy";
    std::string name = base;
    for (int n = 2; resources.findByName<MaterialAsset>(name); ++n) {
        name = base + " " + std::to_string(n);
    }
    copy.name = name;

    MaterialHandle nh = resources.add(std::move(copy));
    if (!nh) return MaterialHandle{};

    if (assignTo) assignTo->material = nh;
    state.markSceneDirty();
    return nh;
}

MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state) {
    MaterialHandle h = generateDefaultMaterial(resources);
    if (!h) return MaterialHandle{};

    // Unique, human-readable name: "Material", then "Material 1", "Material 2"...
    std::string name = "Material";
    for (int n = 1; resources.findByName<MaterialAsset>(name); ++n) {
        name = "Material " + std::to_string(n);
    }
    // The unique name (above) is the material's identity; generateDefaultMaterial
    // shares the "material:default" name, so the rename is what distinguishes
    // each new material on save/load.
    resources.rename(h, name);

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
        case EntityKind::Cube: {
            auto meshHandle = resources.add(generateCube());
            auto matHandle  = generateDefaultMaterial(resources);
            scene.add(entity, Mesh{meshHandle, matHandle});
            break;
        }
        case EntityKind::Sphere: {
            auto meshHandle = resources.add(generateSphere());
            auto matHandle  = generateDefaultMaterial(resources);
            scene.add(entity, Mesh{meshHandle, matHandle});
            break;
        }
        case EntityKind::Plane: {
            auto meshHandle = resources.add(generatePlane());
            auto matHandle  = generateDefaultMaterial(resources);
            scene.add(entity, Mesh{meshHandle, matHandle});
            break;
        }
        case EntityKind::Triangle: {
            auto meshHandle = resources.add(generateTriangle());
            auto matHandle  = generateDefaultMaterial(resources);
            scene.add(entity, Mesh{meshHandle, matHandle});
            break;
        }
        case EntityKind::Pyramid: {
            auto meshHandle = resources.add(generatePyramid());
            auto matHandle  = generateDefaultMaterial(resources);
            scene.add(entity, Mesh{meshHandle, matHandle});
            break;
        }
        case EntityKind::Cone: {
            auto meshHandle = resources.add(generateCone());
            auto matHandle  = generateDefaultMaterial(resources);
            scene.add(entity, Mesh{meshHandle, matHandle});
            break;
        }
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
    auto entity = scene.createEntity();
    EntityId newId = entity.getID();

    if (scene.has<Transform>(source)) {
        auto t = scene.get<Transform>(source);
        t.position += glm::vec3(1.0f, 0.0f, 0.0f);
        scene.add(entity, std::move(t));
    }
    if (scene.has<Name>(source)) {
        auto n = scene.get<Name>(source);
        scene.add(entity, std::move(n));
    }
    if (scene.has<Mesh>(source)) {
        scene.add(entity, scene.get<Mesh>(source));
    }
    if (scene.has<Light>(source)) {
        scene.add(entity, scene.get<Light>(source));
    }
    if (scene.has<Camera>(source)) {
        auto cam = scene.get<Camera>(source);
        cam.active = false;
        scene.add(entity, cam);
    }
    if (scene.has<Rigidbody>(source)) {
        scene.add(entity, scene.get<Rigidbody>(source));
    }
    if (scene.has<Collider>(source)) {
        scene.add(entity, scene.get<Collider>(source));
    }
    if (scene.has<ReflectionProbe>(source)) {
        scene.add(entity, scene.get<ReflectionProbe>(source));
    }
    if (scene.has<Animation>(source)) {
        // Tracks are now copyable - clone the source animation in full
        // (keyframes and all) so duplicated entities keep their motion.
        Animation anim = scene.get<Animation>(source);
        anim.playing   = false;
        scene.add(entity, std::move(anim));
    }
    if (scene.has<ScriptComponent>(source)) {
        // ScriptComponent is move-only - deep-copy each behavior via clone()
        // (authored fields carried over; context rebinds on the new entity).
        const ScriptComponent& src = scene.get<ScriptComponent>(source);
        ScriptComponent copy;
        copy.behaviors.reserve(src.behaviors.size());
        for (const auto& behavior : src.behaviors) {
            if (behavior) copy.behaviors.push_back(behavior->clone());
        }
        scene.add(entity, std::move(copy));
    }

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

void focusOnSelected(FrameContext& ctx, EditorState& state, CameraController& camera) {
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

        if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
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

void frameAll(FrameContext& ctx, CameraController& camera) {
    if (!ctx.visibility || ctx.visibility->entries.empty()) return;

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    bool any = false;
    for (const VisibleEntity& v : ctx.visibility->entries) {
        if (!ctx.scene.has<Mesh>(v.id)) continue;
        const auto& mesh = ctx.scene.get<Mesh>(v.id);
        if (!mesh.mesh || !ctx.resources.isAlive(mesh.mesh)) continue;
        const auto& asset = ctx.resources.get(mesh.mesh);
        if (!hasValidBounds(asset.boundsMin, asset.boundsMax)) continue;

        glm::vec3 wMin, wMax;
        localToWorldAABB(v.model, asset.boundsMin, asset.boundsMax, wMin, wMax);
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
        item("Cube",     EntityKind::Cube);
        item("Sphere",   EntityKind::Sphere);
        item("Plane",    EntityKind::Plane);
        item("Triangle", EntityKind::Triangle);
        item("Pyramid",  EntityKind::Pyramid);
        item("Cone",     EntityKind::Cone);
        ImGui::Separator();
        item("Point Light",       EntityKind::PointLight);
        item("Spot Light",        EntityKind::SpotLight);
        item("Directional Light", EntityKind::DirectionalLight);
        item("Rect Light",        EntityKind::RectLight);
        item("Disk Light",        EntityKind::DiskLight);
        ImGui::Separator();
        item("Camera", EntityKind::Camera);
        item("Reflection Probe", EntityKind::ReflectionProbe);
        ImGui::Separator();
        // The modal can't live here: the menu closes on click and this
        // function stops being called. Defer to drawModelImportDialog().
        if (ImGui::MenuItem("Import Model...")) state.requestModelImport = true;
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
