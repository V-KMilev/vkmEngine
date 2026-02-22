#include "../editor_common.h"

#include "generator/light_generators.h"

namespace Engine {
void EditorSystem::drawInspectorPanel(FrameContext& ctx) {
    if (!m_selectedEntity || !ctx.scene.isAlive(m_selectedEntity)) {
        ImGui::TextDisabled("No entity selected");
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = m_selectedEntity;

    // Name editing
    {
        if (!scene.has<Name>(id)) {
            char fallback[64];
            getEntityDisplayName(scene, id, fallback, sizeof(fallback));
            scene.add(Entity{id}, Name(fallback));
        }
        auto& name = scene.get<Name>(id);
        ImGui::SetNextItemWidth(-60);
        ImGui::InputText("##Name", name.value, sizeof(name.value));
        ImGui::SameLine();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (scene.has<Transform>(id))  drawTransformSection(scene, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, id);
    if (scene.has<Light>(id))      drawLightSection(scene, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, id);
}

void EditorSystem::drawAddComponentMenu(Scene& scene, EntityId id) {
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("##AddComp");
    }

    if (ImGui::BeginPopup("##AddComp")) {
        if (!scene.has<Mesh>(id) && ImGui::MenuItem("Mesh")) {
            scene.add(Entity{id}, Mesh{});
        }
        if (!scene.has<Light>(id) && ImGui::MenuItem("Light")) {
            scene.add(Entity{id}, generatePointLight());
        }
        if (!scene.has<Camera>(id) && ImGui::MenuItem("Camera")) {
            Camera cam;
            cam.active = false;
            scene.add(Entity{id}, cam);
        }
        if (!scene.has<Animation>(id) && ImGui::MenuItem("Animation")) {
            scene.add(Entity{id}, Animation{});
        }
        ImGui::EndPopup();
    }
}
void EditorSystem::drawTransformSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    auto& t = scene.get<Transform>(id);
    drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (drawVec3Control("Rotation", glm::value_ptr(euler), 0.0f, 0.5f)) {
        t.rotation = glm::quat(glm::radians(euler));
    }

    drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

    if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
        glm::mat4 worldMat = HierarchyUtils::computeWorldMatrix(scene, id);
        glm::vec3 worldPos(worldMat[3]);
        ImGui::TextDisabled("  World: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y, worldPos.z);
    }
    ImGui::Spacing();
}

void EditorSystem::drawMeshSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Mesh##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Mesh", id.index)) { scene.remove<Mesh>(Entity{id}); return; }

    if (!open) return;
    auto& mesh = scene.get<Mesh>(id);
    drawPropertyLabel("Mesh ID");    ImGui::Text("#%u", mesh.mesh.id());
    drawPropertyLabel("Material ID"); ImGui::Text("#%u", mesh.material.id());
    drawPropertyLabel("Visible");    ImGui::Checkbox("##MeshVis", &mesh.visible);
    ImGui::Spacing();
}

void EditorSystem::drawLightSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Light##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Light", id.index)) { scene.remove<Light>(Entity{id}); return; }

    if (!open) return;
    auto& light = scene.get<Light>(id);

    drawPropertyLabel("Type");
    const char* typeNames[] = {"Directional", "Point", "Spot"};
    int typeIdx = static_cast<int>(light.type);
    if (ImGui::Combo("##LType", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
        light.type = static_cast<LightType>(typeIdx);
    }

    drawPropertyLabel("Color");
    ImGui::ColorEdit3("##LColor", glm::value_ptr(light.color), ImGuiColorEditFlags_Float);

    drawPropertyLabel("Intensity");
    ImGui::DragFloat("##LIntensity", &light.intensity, 0.1f, 0.0f, 100.0f, "%.1f");

    if (light.type != LightType::Directional) {
        drawPropertyLabel("Radius");
        ImGui::DragFloat("##LRadius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");
    }

    if (light.type == LightType::Spot) {
        float innerDeg = glm::degrees(light.innerConeAngle);
        float outerDeg = glm::degrees(light.outerConeAngle);
        drawPropertyLabel("Inner Cone");
        if (ImGui::DragFloat("##InnerC", &innerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
            light.innerConeAngle = glm::radians(innerDeg);
        drawPropertyLabel("Outer Cone");
        if (ImGui::DragFloat("##OuterC", &outerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg"))
            light.outerConeAngle = glm::radians(outerDeg);
    }

    drawPropertyLabel("Shadows");  ImGui::Checkbox("##Shad", &light.castShadows);
    drawPropertyLabel("Enabled");  ImGui::Checkbox("##LEn", &light.enabled);
    ImGui::Spacing();
}

void EditorSystem::drawCameraSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Camera##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Camera", id.index)) { scene.remove<Camera>(Entity{id}); return; }

    if (!open) return;
    auto& cam = scene.get<Camera>(id);

    drawPropertyLabel("Projection");
    const char* projNames[] = {"Perspective", "Orthographic"};
    int projIdx = static_cast<int>(cam.projection);
    if (ImGui::Combo("##CProj", &projIdx, projNames, IM_ARRAYSIZE(projNames)))
        cam.projection = static_cast<ProjectionType>(projIdx);

    if (cam.projection == ProjectionType::Perspective) {
        float fovDeg = glm::degrees(cam.fovY);
        drawPropertyLabel("FOV");
        if (ImGui::SliderFloat("##CFOV", &fovDeg, 10.0f, 170.0f, "%.0f deg"))
            cam.fovY = glm::radians(fovDeg);
    } else {
        drawPropertyLabel("Ortho Height");
        ImGui::DragFloat("##COrthoH", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
    }

    drawPropertyLabel("Near Clip"); ImGui::DragFloat("##CNear", &cam.zNear, 0.01f, 0.001f, cam.zFar, "%.3f");
    drawPropertyLabel("Far Clip");  ImGui::DragFloat("##CFar", &cam.zFar, 1.0f, cam.zNear, 100000.0f, "%.0f");
    drawPropertyLabel("Exposure");  ImGui::DragFloat("##CExp", &cam.exposure, 0.01f, 0.0f, 10.0f, "%.2f");
    drawPropertyLabel("Active");    ImGui::Checkbox("##CAct", &cam.active);
    ImGui::Spacing();
}

void EditorSystem::drawAnimationSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Animation##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Animation", id.index)) { scene.remove<Animation>(Entity{id}); return; }

    if (!open) return;
    auto& anim = scene.get<Animation>(id);

    float btnW = ImGui::GetFrameHeight();
    bool playing = anim.playing;
    if (playing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.48f, 0.76f, 1.0f));
    if (ImGui::Button(playing ? "||" : ">", ImVec2(btnW * 1.5f, 0))) anim.playing = !anim.playing;
    if (playing) ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("|<", ImVec2(btnW * 1.5f, 0))) anim.time = 0.0f;
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &anim.looping);

    if (anim.duration > 0.0f) {
        ImGui::SetNextItemWidth(-1);
        char timeFmt[32];
        snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", anim.duration);
        ImGui::SliderFloat("##ATime", &anim.time, 0.0f, anim.duration, timeFmt);
        ImVec4 barCol = anim.playing ? ImVec4(0.33f, 0.56f, 0.88f, 1.0f) : ImVec4(0.40f, 0.40f, 0.44f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
        ImGui::ProgressBar(anim.time / anim.duration, ImVec2(-1, 3), "");
        ImGui::PopStyleColor();
    }

    drawPropertyLabel("Speed"); ImGui::DragFloat("##ASpeed", &anim.speed, 0.01f, 0.0f, 10.0f, "%.2fx");
    ImGui::TextDisabled("Tracks: %s%s%s  Dur: %.2fs",
        anim.positionTrack.isEmpty() ? "" : "P ",
        anim.rotationTrack.isEmpty() ? "" : "R ",
        anim.scaleTrack.isEmpty() ? "" : "S ",
        anim.duration);
    ImGui::Spacing();
}

void EditorSystem::drawHierarchySection(Scene& scene, EntityId id) {
    if (!ImGui::CollapsingHeader("Hierarchy##Sec")) return;
    const auto& h = scene.get<Hierarchy>(id);

    if (h.parent) {
        ImGui::TextDisabled("Parent:");
        ImGui::SameLine();
        char parentName[64];
        getEntityDisplayName(scene, h.parent, parentName, sizeof(parentName));
        if (ImGui::SmallButton(parentName)) {
            m_selectedEntity = h.parent;
        }
    } else {
        ImGui::TextDisabled("Root (no parent)");
    }

    if (h.firstChild) {
        ImGui::TextDisabled("Children:");
        HierarchyUtils::forEachChild(scene, id, [&](EntityId child) {
            char icon[8], name[64];
            getEntityIcon(scene, child, icon, sizeof(icon));
            getEntityDisplayName(scene, child, name, sizeof(name));
            char lbl[96];
            snprintf(lbl, sizeof(lbl), "  %s %s", icon, name);
            if (ImGui::Selectable(lbl, m_selectedEntity == child)) {
                m_selectedEntity = child;
            }
        });
    }
    ImGui::Spacing();
}

} // namespace Engine
