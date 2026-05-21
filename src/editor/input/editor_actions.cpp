#include "input/editor_actions.h"
#include "framework/editor_state.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "system/visibility/visibility.h"
#include "system/visibility/bounds_utils.h"
#include "system/camera/camera_controller.h"

#include "generator/light_generators.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"
#include "loader/model_loader.h"

#include <string>
#include <filesystem>

namespace Engine {
namespace EditorActions {

namespace {
    const char* defaultName(EntityKind k) {
        switch (k) {
            case EntityKind::Empty:            return "Empty";
            case EntityKind::Cube:             return "Cube";
            case EntityKind::Sphere:           return "Sphere";
            case EntityKind::PointLight:       return "Point Light";
            case EntityKind::SpotLight:        return "Spot Light";
            case EntityKind::DirectionalLight: return "Directional Light";
            case EntityKind::Camera:           return "Camera";
        }
        return "Entity";
    }
}

EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityKind kind) {
    auto entity = scene.createEntity();
    EntityId id = entity.getID();

    scene.add(entity, Transform{});
    scene.add(entity, Name(defaultName(kind)));

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
        case EntityKind::Camera: {
            Camera cam;
            cam.active = false;
            scene.add(entity, cam);
            break;
        }
    }

    state.hierarchyDirty = true;
        state.markSceneDirty();
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
    if (scene.has<Animation>(source)) {
        Animation anim;
        anim.duration = scene.get<Animation>(source).duration;
        anim.speed = scene.get<Animation>(source).speed;
        anim.looping = scene.get<Animation>(source).looping;
        anim.playing = false;
        scene.add(entity, std::move(anim));
    }

    state.hierarchyDirty = true;
        state.markSceneDirty();
    state.selectedEntity = newId;
}

void deleteEntity(Scene& scene, EditorState& state, EntityId entity) {
    if (state.selectedEntity == entity) state.selectedEntity = {};

    if (scene.has<Hierarchy>(entity) && scene.get<Hierarchy>(entity).firstChild) {
        HierarchyOperations::destroyHierarchy(scene, entity);
    } else {
        if (scene.has<Hierarchy>(entity)) {
            HierarchyOperations::removeFromParent(scene, entity);
        }
        scene.destroyEntity(Entity{entity});
    }
    state.hierarchyDirty = true;
        state.markSceneDirty();
}

void focusOnSelected(FrameContext& ctx, EditorState& state, CameraController& camera) {
    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) return;
    if (!ctx.scene.has<Transform>(state.selectedEntity)) return;

    bool hasParent = ctx.scene.has<Hierarchy>(state.selectedEntity)
                  && ctx.scene.get<Hierarchy>(state.selectedEntity).parent;

    glm::vec3 targetPos;
    float focusDistance = 5.0f;

    if (ctx.scene.has<Mesh>(state.selectedEntity)) {
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
    // Fit a sphere of radius diag/2 in the perspective frustum. Assume a
    // ~50 deg vertical FOV target; a 1.5x pad gives breathing room. The
    // camera controller normalises the look direction.
    const float distance = std::max(2.0f, diag * 1.1f);
    camera.focusOn(ctx.scene, center, distance);
}

void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (ImGui::BeginMenu("Create")) {
        auto item = [&](const char* label, EntityKind k) {
            if (ImGui::MenuItem(label)) state.selectedEntity = createEntity(scene, resources, state, k);
        };
        item("Empty Entity", EntityKind::Empty);
        ImGui::Separator();
        item("Cube",   EntityKind::Cube);
        item("Sphere", EntityKind::Sphere);
        ImGui::Separator();
        item("Point Light",       EntityKind::PointLight);
        item("Spot Light",        EntityKind::SpotLight);
        item("Directional Light", EntityKind::DirectionalLight);
        ImGui::Separator();
        item("Camera", EntityKind::Camera);
        ImGui::Separator();
        // The modal can't live here: the menu closes on click and this
        // function stops being called. Defer to drawModelImportDialog().
        if (ImGui::MenuItem("Import Model...")) state.requestModelImport = true;
        ImGui::EndMenu();
    }
}

void ModelImportDialog::draw(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        const std::filesystem::path appRoot = APP_ROOT_DIR;
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
            (std::filesystem::path(APP_ROOT_DIR) / picked).string(),
            resources, scene);
        if (rootId) {
            state.selectedEntity = rootId;
            state.hierarchyDirty = true;
        state.markSceneDirty();
        }
    }
}

} // namespace EditorActions
} // namespace Engine
