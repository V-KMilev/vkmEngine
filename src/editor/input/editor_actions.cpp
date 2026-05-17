#include "input/editor_actions.h"
#include "framework/editor_state.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include <cstring>

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
#include "system/visibility/bounds_utils.h"
#include "system/camera/camera_controller.h"

#include "generator/light_generators.h"
#include "generator/mesh_generators.h"
#include "generator/material_generators.h"
#include "loader/model_loader.h"

#include <cctype>
#include <string>
#include <filesystem>
#include <system_error>

namespace Engine {
namespace EditorActions {

EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, const char* type) {
    auto entity = scene.createEntity();
    EntityId id = entity.getID();

    scene.add(entity, Transform{});
    scene.add(entity, Name(type));

    if (std::strcmp(type, "Point Light") == 0) {
        scene.add(entity, generatePointLight());
    } else if (std::strcmp(type, "Spot Light") == 0) {
        scene.add(entity, generateSpotLight());
    } else if (std::strcmp(type, "Directional Light") == 0) {
        scene.add(entity, generateDirectionalLight());
    } else if (std::strcmp(type, "Cube") == 0) {
        auto meshHandle = resources.add(generateCube());
        auto matHandle = generateDefaultMaterial(resources);
        scene.add(entity, Mesh{meshHandle, matHandle});
    } else if (std::strcmp(type, "Sphere") == 0) {
        auto meshHandle = resources.add(generateSphere());
        auto matHandle = generateDefaultMaterial(resources);
        scene.add(entity, Mesh{meshHandle, matHandle});
    } else if (std::strcmp(type, "Camera") == 0) {
        Camera cam;
        cam.active = false;
        scene.add(entity, cam);
    }

    state.hierarchyDirty = true;
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
}

void focusOnSelected(FrameContext& ctx, EditorState& state, CameraController* camera) {
    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) return;
    if (!ctx.scene.has<Transform>(state.selectedEntity)) return;
    if (!camera) return;

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

    camera->focusOn(ctx.scene, targetPos, focusDistance);
}

void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Empty Entity")) {
            state.selectedEntity = createEntity(scene, resources, state, "Empty");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Cube"))    state.selectedEntity = createEntity(scene, resources, state, "Cube");
        if (ImGui::MenuItem("Sphere"))  state.selectedEntity = createEntity(scene, resources, state, "Sphere");
        ImGui::Separator();
        if (ImGui::MenuItem("Point Light"))       state.selectedEntity = createEntity(scene, resources, state, "Point Light");
        if (ImGui::MenuItem("Spot Light"))        state.selectedEntity = createEntity(scene, resources, state, "Spot Light");
        if (ImGui::MenuItem("Directional Light")) state.selectedEntity = createEntity(scene, resources, state, "Directional Light");
        ImGui::Separator();
        if (ImGui::MenuItem("Camera"))  state.selectedEntity = createEntity(scene, resources, state, "Camera");
        ImGui::Separator();
        // The modal can't live here: the menu closes on click and this
        // function stops being called. Defer to drawModelImportDialog().
        if (ImGui::MenuItem("Import Model...")) state.requestModelImport = true;
        ImGui::EndMenu();
    }
}

void drawModelImportDialog(Scene& scene, ResourceManager& resources, EditorState& state) {
    if (state.requestModelImport) {
        ImGui::OpenPopup("Import Model");
        state.requestModelImport = false;
    }
    if (!ImGui::BeginPopupModal("Import Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    auto isModelExt = [](const std::string& e) {
        return e == ".gltf" || e == ".glb" || e == ".obj" || e == ".fbx"
            || e == ".dae"  || e == ".stl" || e == ".ply" || e == ".3ds";
    };

    const std::filesystem::path root = std::filesystem::path(APP_ROOT_DIR) / "assets";
    ImGui::TextDisabled("%s", root.string().c_str());
    ImGui::TextDisabled("glTF / GLB / OBJ / FBX / DAE / STL / PLY / 3DS");
    ImGui::Separator();
    std::error_code ec;
    int shown = 0;
    for (const auto& entry :
            std::filesystem::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!isModelExt(ext)) continue;
        std::error_code rel_ec;
        const std::string rel = std::filesystem::relative(
            entry.path(), std::filesystem::path(APP_ROOT_DIR), rel_ec)
            .generic_string();
        if (ImGui::Selectable(rel.c_str())) {
            EntityId rootId =
                importModelIntoScene(entry.path().string(), resources, scene);
            if (rootId) {
                state.selectedEntity = rootId;
                state.hierarchyDirty = true;
            }
            ImGui::CloseCurrentPopup();
        }
        if (++shown > 2000) break;  // safety cap
    }
    if (shown == 0) ImGui::TextDisabled("(no supported model files under assets/)");
    ImGui::Separator();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace EditorActions
} // namespace Engine
