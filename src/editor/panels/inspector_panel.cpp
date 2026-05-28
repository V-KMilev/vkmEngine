#include "panels/inspector_panel.h"

#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "framework/editor_common.h"
#include "framework/editor_commands.h"
#include "ecs/component/reflection_probe.h"
#include "system/render/render_view.h"   // EnvironmentConfig
#include "system/visibility/bounds_utils.h"
#include "input/editor_actions.h"
#include "generator/light_generators.h"

namespace Engine {

namespace {
    // Per-component accent colors - the left strip / guide line that lets the
    // eye group a card at a glance (Transform blue, Mesh green, ...).
    const ImVec4 ACCENT_TRANSFORM = EditorStyle::AXIS_Z;
    const ImVec4 ACCENT_MESH      = EditorStyle::AXIS_Y;
    const ImVec4 ACCENT_LIGHT     = ImVec4(1.00f, 0.80f, 0.22f, 1.0f);
    const ImVec4 ACCENT_CAMERA    = ImVec4(0.30f, 0.78f, 0.80f, 1.0f);
    const ImVec4 ACCENT_ANIM      = ImVec4(0.64f, 0.44f, 0.86f, 1.0f);
    const ImVec4 ACCENT_HIERARCHY = ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
}

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    drawPanelTitle("Inspector");

    if (!state.selectedEntity || !ctx.scene.isAlive(state.selectedEntity)) {
        // Centered empty-state with a large neutral glyph + two-line hint and
        // a quick "create entity" affordance, so a fresh user has somewhere
        // to go from a blank panel.
        const ImVec2 region = ImGui::GetContentRegionAvail();
        const float glyphSize = 56.0f;
        const float lineH     = ImGui::GetTextLineHeightWithSpacing();
        const float blockH    = glyphSize + lineH * 2.0f + ImGui::GetFrameHeight() + 24.0f;
        ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (region.y - blockH) * 0.35f)));

        const ImVec2 cur = ImGui::GetCursorScreenPos();
        const float iconCx = cur.x + region.x * 0.5f;
        ImGui::Dummy(ImVec2(0.0f, glyphSize));
        drawEditorIcon(ImGui::GetWindowDrawList(), EditorIcon::Select,
            ImVec2(iconCx, cur.y + glyphSize * 0.5f), glyphSize * 0.40f,
            ImGui::GetColorU32(ImGuiCol_TextDisabled));

        ImGui::Spacing();
        const char* line1 = "No entity selected";
        const char* line2 = "Pick one in the Hierarchy, or click in the viewport.";
        ImGui::SetCursorPosX((region.x - ImGui::CalcTextSize(line1).x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::HEADER_TEXT);
        ImGui::TextUnformatted(line1);
        ImGui::PopStyleColor();
        ImGui::SetCursorPosX((region.x - ImGui::CalcTextSize(line2).x) * 0.5f);
        ImGui::TextDisabled("%s", line2);

        ImGui::Spacing();
        const float btnW = 180.0f;
        ImGui::SetCursorPosX((region.x - btnW) * 0.5f);
        if (ImGui::Button("+  Create Entity", ImVec2(btnW, 0.0f)))
            ImGui::OpenPopup("##EmptyCreate");
        if (ImGui::BeginPopup("##EmptyCreate")) {
            EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
            ImGui::EndPopup();
        }
        return;
    }

    auto& scene = ctx.scene;
    EntityId id = state.selectedEntity;

    // Identity header: type badge + name (or "Add name" affordance) + id.
    // Naming is opt-in - the inspector never adds Name during draw, only on
    // explicit user action, so a glance at an entity doesn't mutate the scene.
    {
        const float ih = ImGui::GetFrameHeight();
        inlineIcon(entityIconKind(scene, id), ih,
                   ImGui::GetColorU32(EditorStyle::ACCENT));
        ImGui::SameLine();

        if (scene.has<Name>(id)) {
            auto& name = scene.get<Name>(id);
            ImGui::SetNextItemWidth(-46.0f);
            ImGui::InputText("##Name", name.value, sizeof(name.value));
        } else {
            char fallback[64];
            getEntityDisplayName(scene, id, fallback, sizeof(fallback));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fallback);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##addname")) scene.add(Entity{id}, Name(fallback));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
            ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 46.0f);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // The Environment singleton owns the rendering/post stack - render that
    // instead of the usual component cards, and skip Add Component.
    if (scene.has<EnvironmentConfig>(id)) {
        m_environmentUI.draw(ec, scene.get<EnvironmentConfig>(id));
        return;
    }

    if (scene.has<Transform>(id))  drawTransformSection(scene, state, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, state, id);
    if (scene.has<Light>(id))      drawLightSection(scene, state, id);
    if (scene.has<ReflectionProbe>(id)) drawReflectionProbeSection(scene, state, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, state, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, state, id);
    if (scene.has<Hierarchy>(id))  drawHierarchySection(scene, state, id);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, state, id);
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id) {
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    const bool clicked = ImGui::Button("+  Add Component", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (clicked) ImGui::OpenPopup("##AddComp");

    if (ImGui::BeginPopup("##AddComp")) {
        ImGui::TextDisabled("Add Component");
        ImGui::Separator();
        // Each add routes through AddComponentCommand so undo can drop the
        // component the user just added. The component value captured in
        // the command is the same one we add to the scene.
        if (!scene.has<Mesh>(id) && ImGui::MenuItem("Mesh")) {
            Mesh m{};
            scene.add(Entity{id}, m);
            state.commands.push(std::make_unique<AddComponentCommand<Mesh>>(id, m, "Add Mesh"));
            state.markSceneDirty();
        }
        if (!scene.has<Light>(id) && ImGui::MenuItem("Light")) {
            Light l = generatePointLight();
            scene.add(Entity{id}, l);
            state.commands.push(std::make_unique<AddComponentCommand<Light>>(id, l, "Add Light"));
            state.markSceneDirty();
        }
        if (!scene.has<Camera>(id) && ImGui::MenuItem("Camera")) {
            Camera cam;
            cam.active = false;
            scene.add(Entity{id}, cam);
            state.commands.push(std::make_unique<AddComponentCommand<Camera>>(id, cam, "Add Camera"));
            state.markSceneDirty();
        }
        if (!scene.has<Animation>(id) && ImGui::MenuItem("Animation")) {
            Animation a{};
            scene.add(Entity{id}, a);
            state.commands.push(std::make_unique<AddComponentCommand<Animation>>(
                id, std::move(a), "Add Animation"));
            state.markSceneDirty();
        }
        if (!scene.has<ReflectionProbe>(id) && ImGui::MenuItem("Reflection Probe")) {
            ReflectionProbe p{};
            scene.add(Entity{id}, p);
            state.commands.push(std::make_unique<AddComponentCommand<ReflectionProbe>>(
                id, p, "Add Reflection Probe"));
            state.markSceneDirty();
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawTransformSection(Scene& scene, EditorState& state, EntityId id) {
    // Transform is intrinsic - no remove affordance.
    const bool open = beginComponentCard("Transform", ACCENT_TRANSFORM, true);
    if (open) {
        auto& t = scene.get<Transform>(id);
        bool changed = false;
        changed |= drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

        m_eulerCache.sync(id, t.rotation);
        if (drawVec3Control("Rotation", m_eulerCache.degrees(), 0.0f, 0.5f)) {
            t.rotation = m_eulerCache.toQuat();
            changed = true;
        }

        changed |= drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

        if (changed) {
            HierarchyOperations::markDirty(scene, id);
            state.markSceneDirty();
        }

        if (scene.has<Hierarchy>(id) && scene.get<Hierarchy>(id).parent) {
            glm::mat4 worldMat = HierarchyOperations::computeWorldMatrix(scene, id);
            glm::vec3 worldPos(worldMat[3]);
            ImGui::TextDisabled("World: (%.1f, %.1f, %.1f)",
                worldPos.x, worldPos.y, worldPos.z);
        }
    }
    endComponentCard();
}

void InspectorPanel::drawMeshSection(Scene& scene, ResourceManager& resources,
                                     EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Mesh", ACCENT_MESH, true, &remove);
    if (open) {
        auto& mesh = scene.get<Mesh>(id);
        bool changed = false;

        drawPropertyLabel("Visible");      changed |= ImGui::Checkbox("##MeshVis", &mesh.visible);
        drawPropertyLabel("Cast Shadow");  changed |= ImGui::Checkbox("##MeshShad", &mesh.castShadows);

        if (mesh.mesh) {
            const auto& asset = resources.get(mesh.mesh);
            ImGui::TextDisabled("%zu verts, %zu tris",
                asset.vertices.size(), asset.indices.size() / 3);
            if (hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                glm::vec3 ext = asset.boundsMax - asset.boundsMin;
                ImGui::TextDisabled("Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
            }
        }

        ImGui::Spacing();

        // Asset pickers: swap which loaded mesh / material this component uses.
        // Combos snapshot the asset list into a local vector so ImGuiListClipper
        // can window the visible rows - keeps the combo fluid even with
        // thousands of materials.
        auto pickAsset = [](const char* id, const char* label, auto& resources,
                            auto& currentHandle, auto* tag) -> bool {
            using Asset = std::remove_pointer_t<decltype(tag)>;
            using Handle = std::remove_reference_t<decltype(currentHandle)>;
            const std::string cur = currentHandle
                ? resources.template get(currentHandle).name : std::string("(none)");
            drawPropertyLabel(label);
            ImGui::SetNextItemWidth(-1.0f);
            if (!ImGui::BeginCombo(id, cur.empty() ? "(unnamed)" : cur.c_str()))
                return false;

            std::vector<std::pair<Handle, const Asset*>> rows;
            resources.template forEachOfType<Asset>(
                [&](Handle h, const Asset& a) {
                    if (a.editorOnly) return;
                    rows.emplace_back(h, &a);
                });

            bool picked = false;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& [h, a] = rows[i];
                    ImGui::PushID(static_cast<int>(h.id()));
                    const bool sel = currentHandle && currentHandle.id() == h.id();
                    if (ImGui::Selectable(a->name.empty() ? "(unnamed)" : a->name.c_str(), sel)) {
                        currentHandle = h;
                        picked = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
            return picked;
        };

        changed |= pickAsset("##MeshPick", "Mesh Asset",     resources, mesh.mesh,     (MeshAsset*)nullptr);
        changed |= pickAsset("##MatPick",  "Material Asset", resources, mesh.material, (MaterialAsset*)nullptr);

        ImGui::Spacing();

        // Material - compact reference. Full PBR + texture editing and the
        // live 3D preview live in the Material Editor (Window > Material
        // Editor).
        if (mesh.material) {
            const MaterialAsset& m = resources.get(mesh.material);
            ImGui::TextDisabled("Material: %s",
                m.name.empty() ? "(unnamed)" : m.name.c_str());
            const float bw = (ImGui::GetContentRegionAvail().x
                              - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Edit Material", ImVec2(bw, 0))) {
                state.materialEditorTarget = mesh.material;
                state.showMaterialEditor   = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate", ImVec2(bw, 0))) {
                if (MaterialHandle nh = EditorActions::duplicateMaterial(resources, state, mesh.material, &mesh)) {
                    state.materialEditorTarget = nh;
                    state.showMaterialEditor   = true;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fork the material and edit the copy safely");
        } else {
            ImGui::TextDisabled("No material assigned");
        }

        if (changed) state.markSceneDirty();
    }
    endComponentCard();
    if (remove) {
        // Snapshot before removal so undo can restore the exact component.
        Mesh snap = scene.get<Mesh>(id);
        scene.remove<Mesh>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Mesh>>(id, snap, "Remove Mesh"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawLightSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Light", ACCENT_LIGHT, true, &remove);
    if (open) {
        auto& light = scene.get<Light>(id);
        bool changed = false;

        drawPropertyLabel("Type");
        const char* typeNames[] = {"Directional", "Point", "Spot", "Rect", "Disk"};
        int typeIdx = static_cast<int>(light.type);
        if (ImGui::Combo("##LType", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
            light.type = static_cast<LightType>(typeIdx);
            changed = true;
        }

        drawPropertyLabel("Color");
        changed |= ImGui::ColorEdit3("##LColor", glm::value_ptr(light.color), ImGuiColorEditFlags_Float);

        // Intensity is unbounded on the upper end (HDR scenes routinely need
        // values in the hundreds for sun, thousands for studio lights). The
        // drag range only clamps soft; users can type any value.
        drawPropertyLabel("Intensity");
        changed |= ImGui::DragFloat("##LIntensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.2f");

        if (light.type != LightType::Directional) {
            drawPropertyLabel("Radius");
            changed |= ImGui::DragFloat("##LRadius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");
        }

        if (light.type == LightType::Spot) {
            float innerDeg = glm::degrees(light.innerConeAngle);
            float outerDeg = glm::degrees(light.outerConeAngle);
            drawPropertyLabel("Inner Cone");
            if (ImGui::DragFloat("##InnerC", &innerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg")) {
                light.innerConeAngle = glm::radians(innerDeg);
                changed = true;
            }
            drawPropertyLabel("Outer Cone");
            if (ImGui::DragFloat("##OuterC", &outerDeg, 0.5f, 0.0f, 90.0f, "%.1f deg")) {
                light.outerConeAngle = glm::radians(outerDeg);
                changed = true;
            }
        }

        if (light.type == LightType::Rect) {
            drawPropertyLabel("Width");
            changed |= ImGui::DragFloat("##LRectW", &light.areaWidth, 0.05f, 0.01f, 100.0f, "%.2f");
            drawPropertyLabel("Height");
            changed |= ImGui::DragFloat("##LRectH", &light.areaHeight, 0.05f, 0.01f, 100.0f, "%.2f");
            drawPropertyLabel("Two-sided");
            changed |= ImGui::Checkbox("##LRectTS", &light.twoSided);
        }
        if (light.type == LightType::Disk) {
            drawPropertyLabel("Disk Radius");
            changed |= ImGui::DragFloat("##LDiskR", &light.areaRadius, 0.05f, 0.01f, 100.0f, "%.2f");
            drawPropertyLabel("Two-sided");
            changed |= ImGui::Checkbox("##LDiskTS", &light.twoSided);
        }
        if (light.type == LightType::Rect || light.type == LightType::Disk) {
            ImGui::TextDisabled("LTC Lambertian diffuse + representative-point GGX specular.");
        }

        drawPropertyLabel("Shadows");  changed |= ImGui::Checkbox("##Shad", &light.castShadows);
        if (light.castShadows) {
            drawPropertyLabel("Shadow Bias");
            changed |= ImGui::DragFloat("##ShadBias", &light.shadowBias, 0.0005f, 0.0f, 0.1f, "%.4f");
            if (light.type == LightType::Directional) {
                drawPropertyLabel("Shadow Extent");
                changed |= ImGui::DragFloat("##ShadExt", &light.shadowExtent, 1.0f, 1.0f, 1000.0f, "%.1f");
            }
        }
        drawPropertyLabel("Enabled");  changed |= ImGui::Checkbox("##LEn", &light.enabled);

        if (changed) state.markSceneDirty();
    }
    endComponentCard();
    if (remove) {
        Light snap = scene.get<Light>(id);
        scene.remove<Light>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Light>>(id, snap, "Remove Light"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawReflectionProbeSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Reflection Probe", ACCENT_LIGHT, true, &remove);
    if (open) {
        auto& probe = scene.get<ReflectionProbe>(id);
        bool changed = false;

        // HDR source path. Bumping bakeVersion when the path changes
        // signals the backend's IBL bake to re-run for this probe.
        drawPropertyLabel("HDR Path");
        char buf[512];
        std::strncpy(buf, probe.hdrPath.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("##PHdr", buf, sizeof(buf))) {
            probe.hdrPath = buf;
            ++probe.bakeVersion;
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Path to the equirect HDR (.hdr) that this probe bakes from.\n"
                              "Empty = this probe contributes nothing; the global IBL bake\n"
                              "is used inside its radius instead.");

        drawPropertyLabel("Radius");
        changed |= ImGui::DragFloat("##PRadius", &probe.radius, 0.1f, 0.1f, 1000.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sphere of influence in world units. Outside this distance\n"
                              "the probe contributes nothing.");

        drawPropertyLabel("Falloff");
        changed |= ImGui::SliderFloat("##PFalloff", &probe.falloffRange, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inner radius (as fraction of Radius) where the probe is at\n"
                              "full strength. From there to Radius the weight smooths out.");

        drawPropertyLabel("Intensity");
        changed |= ImGui::DragFloat("##PIntensity", &probe.intensity, 0.05f, 0.0f, 32.0f, "%.2f");

        ImGui::TextDisabled("Bake version: %d", probe.bakeVersion);

        if (changed) state.markSceneDirty();
    }
    endComponentCard();
    if (remove) {
        ReflectionProbe snap = scene.get<ReflectionProbe>(id);
        scene.remove<ReflectionProbe>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<ReflectionProbe>>(
            id, snap, "Remove Reflection Probe"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawCameraSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Camera", ACCENT_CAMERA, true, &remove);
    if (open) {
        auto& cam = scene.get<Camera>(id);
        bool changed = false;

        drawPropertyLabel("Projection");
        const char* projNames[] = {"Perspective", "Orthographic"};
        int projIdx = static_cast<int>(cam.projection);
        if (ImGui::Combo("##CProj", &projIdx, projNames, IM_ARRAYSIZE(projNames))) {
            cam.projection = static_cast<ProjectionType>(projIdx);
            changed = true;
        }

        if (cam.projection == ProjectionType::Perspective) {
            float fovDeg = glm::degrees(cam.fovY);
            drawPropertyLabel("FOV");
            if (ImGui::SliderFloat("##CFOV", &fovDeg, 10.0f, 170.0f, "%.0f deg")) {
                cam.fovY = glm::radians(fovDeg);
                changed = true;
            }
        } else {
            drawPropertyLabel("Ortho Height");
            changed |= ImGui::DragFloat("##COrthoH", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
        }

        drawPropertyLabel("Near Clip"); changed |= ImGui::DragFloat("##CNear", &cam.zNear, 0.01f, 0.001f, cam.zFar, "%.3f");
        drawPropertyLabel("Far Clip");  changed |= ImGui::DragFloat("##CFar", &cam.zFar, 1.0f, cam.zNear, 100000.0f, "%.0f");
        drawPropertyLabel("Exposure");  changed |= ImGui::DragFloat("##CExp", &cam.exposure, 0.01f, 0.0f, 10.0f, "%.2f");
        drawPropertyLabel("Active");    changed |= ImGui::Checkbox("##CAct", &cam.active);

        if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
            scene.forEach<Camera>([&](EntityId other, Camera& c) {
                c.active = (other == id);
            });
            changed = true;
        }

        if (changed) state.markSceneDirty();
    }
    endComponentCard();
    if (remove) {
        Camera snap = scene.get<Camera>(id);
        scene.remove<Camera>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Camera>>(id, snap, "Remove Camera"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawAnimationSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Animation", ACCENT_ANIM, true, &remove);
    if (open) {
        auto& anim = scene.get<Animation>(id);

        const float GAP = 8.0f;
        float ih = ImGui::GetFrameHeight();
        if (iconButton("inspPlay", anim.playing ? EditorIcon::Pause : EditorIcon::Play,
                       anim.playing, true, anim.playing ? "Pause" : "Play", ih))
            anim.playing = !anim.playing;
        ImGui::SameLine(0, GAP);
        if (iconButton("inspStop", EditorIcon::Stop, false, true, "Stop (rewind)", ih)) {
            anim.playing = false;
            anim.time = 0.0f;
        }
        ImGui::SameLine(0, GAP);
        ImGui::Checkbox("Loop", &anim.looping);
        ImGui::SameLine(0, GAP);
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
    }
    endComponentCard();
    if (remove) {
        Animation snap = scene.get<Animation>(id);
        scene.remove<Animation>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<Animation>>(
            id, std::move(snap), "Remove Animation"));
        state.markSceneDirty();
    }
}

void InspectorPanel::drawHierarchySection(Scene& scene, EditorState& state, EntityId id) {
    const bool open = beginComponentCard("Hierarchy", ACCENT_HIERARCHY, false);
    if (open) {
        const auto& h = scene.get<Hierarchy>(id);

        bool unparented = false;
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
                EditorActions::commitHierarchyMutation(scene, state, id);
                unparented = true;  // `h` is now stale - skip the rest
            }
        } else {
            ImGui::TextDisabled("Root (no parent). Drag entities in the Hierarchy to parent them.");
        }

        if (!unparented && scene.has<Hierarchy>(id)) {
            const auto& hh = scene.get<Hierarchy>(id);
            if (hh.firstChild) {
                ImGui::TextDisabled("Children:");
                HierarchyOperations::forEachChild(scene, id, [&](EntityId child) {
                    char name[64];
                    getEntityDisplayName(scene, child, name, sizeof(name));
                    char cid[16];
                    snprintf(cid, sizeof(cid), "%u", child.index);
                    if (entitySelectable(cid, state.selectedEntity == child,
                                         entityIconKind(scene, child), name)) {
                        state.selectedEntity = child;
                    }
                });
            }
        }
    }
    endComponentCard();
}

} // namespace Engine
