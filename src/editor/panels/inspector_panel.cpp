#include "panels/inspector_panel.h"
#include "framework/editor_common.h"

#include "generator/light_generators.h"

namespace Engine {

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    // Panel header
    ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
    ImGui::TextUnformatted("Inspector");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) {
        ImGui::TextDisabled("No entity selected");
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = state.selectedEntity;

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
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, id);
    if (scene.has<Light>(id))      drawLightSection(scene, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, state, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, id);
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EntityId id) {
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

void InspectorPanel::drawTransformSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) return;

    auto& t = scene.get<Transform>(id);
    bool changed = false;
    changed |= drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

    // Re-seed the Euler cache only when the rotation changed outside this
    // widget (selection change, gizmo, scene load). While the field is being
    // edited, m_eulerDeg stays the source of truth, so typing 90 in one axis
    // does not make the others snap to +/-180 via gimbal-lock decomposition.
    const glm::quat cached = glm::quat(glm::radians(m_eulerDeg));
    if (!(m_eulerFor == id) || glm::abs(glm::dot(cached, t.rotation)) < 0.9999f) {
        m_eulerDeg = glm::degrees(glm::eulerAngles(t.rotation));
        m_eulerFor = id;
    }
    if (drawVec3Control("Rotation", glm::value_ptr(m_eulerDeg), 0.0f, 0.5f)) {
        t.rotation = glm::normalize(glm::quat(glm::radians(m_eulerDeg)));
        changed = true;
    }

    changed |= drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

    if (changed) HierarchyOperations::markDirty(scene, id);

    if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
        glm::mat4 worldMat = HierarchyOperations::computeWorldMatrix(scene, id);
        glm::vec3 worldPos(worldMat[3]);
        ImGui::TextDisabled("  World: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y, worldPos.z);
    }

    ImGui::Spacing();
}

void InspectorPanel::drawMeshSection(Scene& scene, ResourceManager& resources, EntityId id) {
    bool open = ImGui::CollapsingHeader("Mesh##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Mesh", id.index)) { scene.remove<Mesh>(Entity{id}); return; }

    if (!open) return;
    auto& mesh = scene.get<Mesh>(id);

    drawPropertyLabel("Visible");    ImGui::Checkbox("##MeshVis", &mesh.visible);
    drawPropertyLabel("Cast Shadow");    ImGui::Checkbox("##MeshShad", &mesh.castShadows);

    // Mesh info
    if (mesh.mesh) {
        const auto& asset = resources.get(mesh.mesh);
        ImGui::TextDisabled("  %zu verts, %zu tris",
            asset.vertices.size(), asset.indices.size() / 3);
        if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
            glm::vec3 ext = asset.boundsMax - asset.boundsMin;
            ImGui::TextDisabled("  Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
        }
    }

    ImGui::Spacing();

    // Material properties
    if (mesh.material) {
        bool matOpen = ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen);
        if (matOpen) {
            auto& mat = resources.edit(mesh.material);
            bool changed = false;

            // Material type
            drawPropertyLabel("Type");
            const char* matTypes[] = {"Opaque", "Transparent", "Unlit"};
            int matTypeIdx = static_cast<int>(mat.type);
            if (ImGui::Combo("##MatType", &matTypeIdx, matTypes, IM_ARRAYSIZE(matTypes))) {
                mat.type = static_cast<MaterialType>(matTypeIdx);
                changed = true;
            }

            // Base PBR
            drawPropertyLabel("Albedo");
            changed |= ImGui::ColorEdit4("##Albedo", glm::value_ptr(mat.albedo),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);

            drawPropertyLabel("Metallic");
            changed |= ImGui::SliderFloat("##Metallic", &mat.metallic, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Roughness");
            changed |= ImGui::SliderFloat("##Roughness", &mat.roughness, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Emission");
            changed |= ImGui::ColorEdit3("##Emission", glm::value_ptr(mat.emission),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

            drawPropertyLabel("AO");
            changed |= ImGui::SliderFloat("##AO", &mat.ao, 0.0f, 1.0f, "%.2f");

            drawPropertyLabel("Alpha");
            changed |= ImGui::SliderFloat("##Alpha", &mat.alpha, 0.0f, 1.0f, "%.2f");

            // Surface
            if (ImGui::TreeNode("Surface##Mat")) {
                drawPropertyLabel("IOR");
                changed |= ImGui::DragFloat("##IOR", &mat.ior, 0.01f, 1.0f, 3.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("1.0 air, 1.33 water, 1.5 glass, 2.4 diamond");

                drawPropertyLabel("Transmission");
                changed |= ImGui::SliderFloat("##Trans", &mat.transmission, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Normal Scale");
                changed |= ImGui::DragFloat("##NScale", &mat.normalScale, 0.01f, 0.0f, 5.0f, "%.2f");

                drawPropertyLabel("Height Scale");
                changed |= ImGui::DragFloat("##HScale", &mat.heightScale, 0.001f, 0.0f, 0.5f, "%.3f");

                ImGui::TreePop();
            }

            // Clearcoat
            if (ImGui::TreeNode("Clearcoat##Mat")) {
                drawPropertyLabel("Strength");
                changed |= ImGui::SliderFloat("##CC", &mat.clearcoat, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Roughness");
                changed |= ImGui::SliderFloat("##CCR", &mat.clearcoatRoughness, 0.0f, 1.0f, "%.2f");

                ImGui::TreePop();
            }

            // Anisotropy
            if (ImGui::TreeNode("Anisotropy##Mat")) {
                drawPropertyLabel("Strength");
                changed |= ImGui::SliderFloat("##Aniso", &mat.anisotropy, 0.0f, 1.0f, "%.2f");

                ImGui::TreePop();
            }

            // Subsurface
            if (ImGui::TreeNode("Subsurface##Mat")) {
                drawPropertyLabel("Strength");
                changed |= ImGui::SliderFloat("##SSS", &mat.subsurface, 0.0f, 1.0f, "%.2f");

                drawPropertyLabel("Color");
                changed |= ImGui::ColorEdit3("##SSSCol", glm::value_ptr(mat.subsurfaceColor),
                    ImGuiColorEditFlags_Float);

                ImGui::TreePop();
            }

            if (changed) resources.commit(mesh.material);
            ImGui::TreePop();
        }
    }
    ImGui::Spacing();
}

void InspectorPanel::drawLightSection(Scene& scene, EntityId id) {
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
    if (light.castShadows) {
        drawPropertyLabel("Shadow Bias");
        ImGui::DragFloat("##ShadBias", &light.shadowBias, 0.0005f, 0.0f, 0.1f, "%.4f");
        if (light.type == LightType::Directional) {
            drawPropertyLabel("Shadow Extent");
            ImGui::DragFloat("##ShadExt", &light.shadowExtent, 1.0f, 1.0f, 1000.0f, "%.1f");
        }
    }
    drawPropertyLabel("Enabled");  ImGui::Checkbox("##LEn", &light.enabled);
    ImGui::Spacing();
}

void InspectorPanel::drawCameraSection(Scene& scene, EntityId id) {
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

    if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
        scene.forEach<Camera>([&](EntityId other, Camera& c) {
            c.active = (other == id);
        });
    }
    ImGui::Spacing();
}

void InspectorPanel::drawAnimationSection(Scene& scene, EntityId id) {
    bool open = ImGui::CollapsingHeader("Animation##Sec", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (drawRemoveButton("Animation", id.index)) { scene.remove<Animation>(Entity{id}); return; }

    if (!open) return;
    auto& anim = scene.get<Animation>(id);

    const float kGap = 8.0f;
    float ih = ImGui::GetFrameHeight();
    if (iconButton("inspPlay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                   anim.playing, true, anim.playing ? "Pause" : "Play", ih))
        anim.playing = !anim.playing;
    ImGui::SameLine(0, kGap);
    if (iconButton("inspStop", EditorIcon::Stop, false, true, "Stop (rewind)", ih)) {
        anim.playing = false;
        anim.time = 0.0f;
    }
    ImGui::SameLine(0, kGap);
    ImGui::Checkbox("Loop", &anim.looping);
    ImGui::SameLine(0, kGap);
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##ASpeed", &anim.speed, 0.005f, 0.0f, 10.0f, "Speed %.2fx");

    if (anim.duration > 0.0f) {
        ImGui::SetNextItemWidth(-1);
        char timeFmt[32];
        snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", anim.duration);
        ImGui::SliderFloat("##ATime", &anim.time, 0.0f, anim.duration, timeFmt);
    }

    ImGui::TextDisabled("Tracks: %s%s%s | keys %zu/%zu/%zu",
        anim.positionTrack.isEmpty() ? "" : "P ",
        anim.rotationTrack.isEmpty() ? "" : "R ",
        anim.scaleTrack.isEmpty()    ? "" : "S ",
        anim.positionTrack.keyframeCount(),
        anim.rotationTrack.keyframeCount(),
        anim.scaleTrack.keyframeCount());
    ImGui::Spacing();
}

void InspectorPanel::drawHierarchySection(Scene& scene, EditorState& state, EntityId id) {
    if (!ImGui::CollapsingHeader("Hierarchy##Sec")) return;
    const auto& h = scene.get<Hierarchy>(id);

    if (h.parent) {
        ImGui::TextDisabled("Parent:");
        ImGui::SameLine();
        char parentName[64];
        getEntityDisplayName(scene, h.parent, parentName, sizeof(parentName));
        if (ImGui::SmallButton(parentName)) {
            state.selectedEntity = h.parent;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Unparent")) {
            HierarchyOperations::removeFromParent(scene, id);
            HierarchyOperations::markDirty(scene, id);
            state.hierarchyDirty = true;
            return;  // `h` is now stale (component may have been removed)
        }
    } else {
        ImGui::TextDisabled("Root (no parent). Drag entities in the Hierarchy to parent them.");
    }

    if (h.firstChild) {
        ImGui::TextDisabled("Children:");
        HierarchyOperations::forEachChild(scene, id, [&](EntityId child) {
            char icon[8], name[64];
            getEntityIcon(scene, child, icon, sizeof(icon));
            getEntityDisplayName(scene, child, name, sizeof(name));
            char lbl[96];
            snprintf(lbl, sizeof(lbl), "  %s %s", icon, name);
            if (ImGui::Selectable(lbl, state.selectedEntity == child)) {
                state.selectedEntity = child;
            }
        });
    }
    ImGui::Spacing();
}

} // namespace Engine
