#include "panels/inspector_panel.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ecs/component/animation.h"
#include "ecs/component/collider.h"
#include "ecs/component/reflection_probe.h"
#include "ecs/component/rigidbody.h"
#include "ecs/component/transform.h"
#include "ecs/environment.h"
#include "framework/editor_actions.h"
#include "framework/editor_commands.h"
#include "framework/editor_common.h"
#include "generator/light_generators.h"
#include "io/project_paths.h"
#include "resource/resource_manager.h"
#include "system/physics/collider_fit.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"
#include "core/math/bounds.h"

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
const ImVec4 ACCENT_PHYSICS   = ImVec4(0.36f, 0.78f, 0.45f, 1.0f);
const ImVec4 ACCENT_COLLIDER  = ImVec4(0.25f, 0.65f, 0.40f, 1.0f);
const ImVec4 ACCENT_PROBE     = ImVec4(0.30f, 0.62f, 0.92f, 1.0f);  // reflection probe (blue)
const ImVec4 ACCENT_ENV       = ImVec4(0.45f, 0.66f, 0.95f, 1.0f);  // environment / skybox (sky blue)
const ImVec4 ACCENT_SCRIPT    = ImVec4(0.85f, 0.45f, 0.58f, 1.0f);  // script / behavior (rose)

// Generic reflected-field -> ImGui inspector. The editor only sees a Behavior*,
// so a behavior's authored fields are edited through this visitor (the same
// bridge serialization uses). One DragFloat/Checkbox/etc. per field type;
// `changed` is set the frame any field is edited.
class BehaviorFieldInspector : public BehaviorFieldVisitor {
    public:
        bool changed = false;

        void field(const char* name, float& v) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat(widgetId(name), &v, 0.1f)) changed = true;
        }
        void field(const char* name, int& v) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt(widgetId(name), &v)) changed = true;
        }
        void field(const char* name, bool& v) override {
            drawPropertyLabel(name);
            if (ImGui::Checkbox(widgetId(name), &v)) changed = true;
        }
        void field(const char* name, glm::vec3& v) override {
            drawPropertyLabel(name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat3(widgetId(name), glm::value_ptr(v), 0.1f)) changed = true;
        }

    private:
        // Hidden-label id for the widget; uniqueness across behaviors comes from
        // the per-behavior PushID in drawScriptSection.
        const char* widgetId(const char* name) {
            snprintf(m_id, sizeof(m_id), "##%s", name);
            return m_id;
        }
        char m_id[80] = {};
};

// Asset-reference combo: pick which loaded asset of type Asset a handle points
// at. Snapshots the asset list so ImGuiListClipper can window thousands of rows
// fluidly. Returns true if the selection changed. Used by the Mesh card's
// mesh + material pickers.
template <typename Asset, typename Handle>
bool pickAsset(const char* comboId, const char* label, ResourceManager& resources, Handle& currentHandle) {
    const std::string cur = (currentHandle && resources.isAlive(currentHandle))
        ? resources.get(currentHandle).name : std::string("(none)");
    drawPropertyLabel(label);
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo(comboId, cur.empty() ? "(unnamed)" : cur.c_str()))
        return false;

    std::vector<std::pair<Handle, const Asset*>> rows;
    resources.forEachOfType<Asset>([&](Handle h, const Asset& a) {
        if (a.hidden) return;
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
}

// Shared scaffold for a removable, value-edited component card. Owns the
// remove affordance, the begin/end card pair, the get<T> + `before` snapshot,
// and the two undo pushes (ComponentEditCommand when a field changed, then
// RemoveComponentCommand if the card's remove button was pressed). `drawFields`
// receives the live component and returns whether any field was edited this
// frame. Behavior-identical to the hand-written sections it replaces: the edit
// push happens before endComponentCard, the remove push after.
template <typename T, typename DrawFields>
void editComponentCard(Scene& scene, EditorState& state, EntityId id,
                       const char* title, const ImVec4& accent,
                       const char* editLabel, const char* removeLabel,
                       DrawFields drawFields) {
    bool remove = false;
    const bool open = beginComponentCard(title, accent, true, &remove);
    if (open) {
        auto& component = scene.get<T>(id);
        const T before = component;  // pre-edit value for the undo command
        const bool changed = drawFields(component);
        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<T>>(id, before, component, editLabel));
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        // Snapshot before removal so undo can restore the exact component.
        T snap = scene.get<T>(id);
        scene.remove<T>(Entity{id});
        state.commands.push(std::make_unique<RemoveComponentCommand<T>>(id, snap, removeLabel));
        state.markSceneDirty();
    }
}
}

void InspectorPanel::draw(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    drawPanelTitle("Inspector");

    const bool haveEntity = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
    if (!haveEntity) {
        // The World node (scene-global settings) is selected instead of an entity.
        if (state.worldSelected) { drawWorldInspector(ec); return; }

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
            const Name before = name;
            ImGui::SetNextItemWidth(-46.0f);
            if (ImGui::InputText("##Name", name.value, sizeof(name.value))) {
                // Route through the command stack like every other inspector
                // edit: tryMerge coalesces the keystroke stream into one undo
                // step, and markSceneDirty stops the rename from being silently
                // lost on close (it used to do neither).
                state.commands.push(std::make_unique<ComponentEditCommand<Name>>(
                    id, before, name, "Rename"));
                state.markSceneDirty();
            }
        } else {
            char fallback[64];
            getEntityDisplayName(scene, id, fallback, sizeof(fallback));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(fallback);
            ImGui::SameLine();
            if (ImGui::SmallButton("+##addname")) {
                Name n = makeName(fallback);
                scene.add(Entity{id}, n);
                state.commands.push(std::make_unique<AddComponentCommand<Name>>(id, n, "Add Name"));
                state.markSceneDirty();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
            ImGui::SameLine(0.0f, ImGui::GetContentRegionAvail().x - 46.0f);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("#%u", id.index);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (scene.has<Transform>(id))  drawTransformSection(scene, state, id);
    if (scene.has<Mesh>(id))       drawMeshSection(scene, ctx.resources, state, id);
    if (scene.has<Light>(id))      drawLightSection(scene, state, id);
    if (scene.has<Rigidbody>(id))  drawRigidbodySection(scene, state, id);
    if (scene.has<Collider>(id))   drawColliderSection(scene, ctx.resources, state, id);
    if (scene.has<Camera>(id))     drawCameraSection(scene, state, id);
    if (scene.has<ReflectionProbe>(id)) drawReflectionProbeSection(scene, state, id);
    if (scene.has<Animation>(id))  drawAnimationSection(scene, state, id);
    if (scene.has<ScriptComponent>(id)) drawScriptSection(scene, state, id);
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
        // the command is the same one we add to the scene. The generic lambda
        // collapses the otherwise-identical menu items; the component type is
        // deduced from the prototype value.
        auto addItem = [&](const char* label, auto value, const char* addLabel) {
            using T = decltype(value);
            if (!scene.has<T>(id) && ImGui::MenuItem(label)) {
                scene.add(Entity{id}, value);
                state.commands.push(std::make_unique<AddComponentCommand<T>>(id, std::move(value), addLabel));
                state.markSceneDirty();
            }
        };

        addItem("Mesh", Mesh{}, "Add Mesh");
        addItem("Light", generatePointLight(), "Add Light");
        addItem("Rigidbody", Rigidbody{}, "Add Rigidbody");
        addItem("Collider", Collider{}, "Add Collider");
        Camera cam;
        cam.active = false;
        addItem("Camera", cam, "Add Camera");
        addItem("Reflection Probe", ReflectionProbe{}, "Add Reflection Probe");
        addItem("Animation", Animation{}, "Add Animation");
        // ScriptComponent is move-only, so it can't ride the (value-copying)
        // AddComponentCommand - add it live, like the World/Physics edits.
        if (!scene.has<ScriptComponent>(id) && ImGui::MenuItem("Script")) {
            scene.add(Entity{id}, ScriptComponent{});
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
        const Transform before = t;  // pre-edit value for the coalescing undo command
        bool changed = false;
        changed |= drawVec3Control("Position", glm::value_ptr(t.position), 0.0f, 0.1f);

        m_eulerCache.sync(id, t.rotation);
        if (drawVec3Control("Rotation", m_eulerCache.degrees(), 0.0f, 0.5f)) {
            t.rotation = m_eulerCache.toQuat();
            changed = true;
        }

        changed |= drawVec3Control("Scale", glm::value_ptr(t.scale), 1.0f, 0.01f);

        if (changed) {
            // Coalescing command - tryMerge collapses the per-frame drag stream
            // into one undo step, mirroring the gizmo's drag-end push.
            state.commands.push(std::make_unique<TransformChangeCommand>(id, before, t, "Transform"));
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
    editComponentCard<Mesh>(scene, state, id, "Mesh", ACCENT_MESH, "Edit Mesh", "Remove Mesh",
                            [&](Mesh& mesh) {
        bool changed = false;

        drawPropertyLabel("Visible");      changed |= ImGui::Checkbox("##MeshVis", &mesh.visible);
        drawPropertyLabel("Cast Shadow");  changed |= ImGui::Checkbox("##MeshShad", &mesh.castShadows);

        if (mesh.mesh && resources.isAlive(mesh.mesh)) {
            const auto& asset = resources.get(mesh.mesh);
            ImGui::TextDisabled("%zu verts, %zu tris",
                asset.vertices.size(), asset.indices.size() / 3);
            if (Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                glm::vec3 ext = asset.boundsMax - asset.boundsMin;
                ImGui::TextDisabled("Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
            }
        }

        ImGui::Spacing();

        // Asset pickers: swap which loaded mesh / material this component uses.
        changed |= pickAsset<MeshAsset>    ("##MeshPick", "Mesh Asset",     resources, mesh.mesh);
        changed |= pickAsset<MaterialAsset>("##MatPick",  "Material Asset", resources, mesh.material);

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

        return changed;
    });
}

void InspectorPanel::drawLightSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<Light>(scene, state, id, "Light", ACCENT_LIGHT, "Edit Light", "Remove Light",
                             [&](Light& light) {
        bool changed = false;

        drawPropertyLabel("Type");
        changed |= drawEnumCombo("##LType", light.type);

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
                drawPropertyLabel("Shadow Distance");
                changed |= ImGui::DragFloat("##ShadDist", &light.shadowDistance, 1.0f, 1.0f, 1000.0f, "%.1f");
            }
        }
        drawPropertyLabel("Enabled");  changed |= ImGui::Checkbox("##LEn", &light.enabled);

        return changed;
    });
}

void InspectorPanel::drawWorldInspector(EditorContext& ec) {
    EditorState& state = ec.state;
    Environment& env   = ec.frame.scene.environment();

    // Identity header for the scene's World node.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("World");
    ImGui::SameLine();
    ImGui::TextDisabled(" Scene-global settings");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool open = beginComponentCard("Environment", ACCENT_ENV, true);
    if (open) {
        bool changed = false;

        // Skybox HDR: browse assets/envs via the shared cached AssetPicker
        // instead of a bespoke per-open directory scan. The picker returns the
        // path relative to the project root, so the stored string stays
        // "assets/envs/<file>.hdr" - exactly what the combo wrote and what the
        // IBL baker loads relative to the working dir.
        drawPropertyLabel("Skybox HDR");
        ImGui::TextUnformatted(env.hdrPath.empty() ? "(none)" : env.hdrPath.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Browse...")) {
            const std::filesystem::path appRoot = ProjectPaths::root();
            m_envPicker.options.popupId    = "PickEnvHdr";
            m_envPicker.options.title      = "Pick Environment HDR";
            m_envPicker.options.root       = appRoot / "assets" / "envs";
            m_envPicker.options.recursive  = false;
            m_envPicker.options.kind       = AssetPicker::Kind::Files;
            m_envPicker.options.extensions = {".hdr"};
            m_envPicker.options.relativeTo = appRoot;
            m_envPicker.options.hint.clear();
            m_envPicker.open();
        }
        std::string pickedHdr;
        if (m_envPicker.draw(pickedHdr)) {
            // Normalize to forward slashes so the stored reference matches the
            // combo's generic_string() format across platforms.
            std::string rel = std::filesystem::path(pickedHdr).generic_string();
            if (rel != env.hdrPath) {
                env.hdrPath = rel;
                changed = true;
            }
        }

        drawPropertyLabel("Show Skybox");
        changed |= ImGui::Checkbox("##EnvShow", &env.showSkybox);

        drawPropertyLabel("Brightness");
        changed |= ImGui::SliderFloat("##EnvIntensity", &env.intensity, 0.0f, 3.0f, "%.2f");

        ImGui::TextDisabled("Swapping the HDR re-bakes the IBL (a brief hitch).");

        // Scene-global settings edit live, no per-entity undo command.
        if (changed) state.markSceneDirty();
    }
    endComponentCard();

    // Physics world parameters - also scene-global Environment state, read by
    // PhysicsSystem each fixed step.
    if (beginComponentCard("Physics", ACCENT_PHYSICS, true)) {
        bool changed = false;

        drawPropertyLabel("Gravity");
        changed |= ImGui::DragFloat3("##EnvGravity", glm::value_ptr(env.gravity), 0.05f, -50.0f, 50.0f, "%.2f");

        drawPropertyLabel("Solver Iterations");
        changed |= ImGui::DragInt("##EnvSolverIters", &env.solverIterations, 0.1f, 1, 32);

        if (changed) state.markSceneDirty();
    }
    endComponentCard();
}

void InspectorPanel::drawReflectionProbeSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<ReflectionProbe>(scene, state, id, "Reflection Probe", ACCENT_PROBE,
                                       "Edit Reflection Probe", "Remove Reflection Probe",
                                       [&](ReflectionProbe& probe) {
        bool changed = false;

        // Box half-extents: the influence + parallax-correction box. Should
        // roughly match the surrounding walls of the region the probe represents.
        drawPropertyLabel("Box Size");
        changed |= ImGui::DragFloat3("##ProbeBox", glm::value_ptr(probe.halfExtents), 0.1f, 0.1f, 1000.0f, "%.1f");

        drawPropertyLabel("Falloff");
        changed |= ImGui::SliderFloat("##ProbeFalloff", &probe.falloff, 0.0f, 1.0f, "%.2f");

        drawPropertyLabel("Intensity");
        changed |= ImGui::DragFloat("##ProbeIntensity", &probe.intensity, 0.02f, 0.0f, 8.0f, "%.2f");

        ImGui::Spacing();
        // Box / falloff / intensity are runtime blend params (no re-bake). Moving
        // the probe re-bakes automatically; Rebake forces it after the scene
        // changed (sun moved, geometry edited) by bumping the version.
        if (ImGui::Button("Rebake", ImVec2(-1, 0))) {
            probe.bakeVersion++;
            changed = true;
        }
        ImGui::TextDisabled("Captures the scene from the entity's Transform position.");

        return changed;
    });
}

void InspectorPanel::drawRigidbodySection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<Rigidbody>(scene, state, id, "Rigidbody", ACCENT_PHYSICS,
                                 "Edit Rigidbody", "Remove Rigidbody",
                                 [&](Rigidbody& rb) {
        bool changed = false;

        drawPropertyLabel("Mass");
        changed |= ImGui::DragFloat("##RbMass", &rb.mass, 0.1f, 0.0f, 1000.0f, "%.2f");
        drawPropertyLabel("Static");      changed |= ImGui::Checkbox("##RbStatic", &rb.isStatic);
        drawPropertyLabel("Kinematic");   changed |= ImGui::Checkbox("##RbKinematic", &rb.isKinematic);

        drawPropertyLabel("Gravity Scale");
        changed |= ImGui::DragFloat("##RbGrav", &rb.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f");
        drawPropertyLabel("Restitution");
        changed |= ImGui::DragFloat("##RbRest", &rb.restitution, 0.01f, 0.0f, 1.0f, "%.2f");
        drawPropertyLabel("Friction");
        changed |= ImGui::DragFloat("##RbFric", &rb.friction, 0.01f, 0.0f, 2.0f, "%.2f");
        drawPropertyLabel("Linear Damping");
        changed |= ImGui::DragFloat("##RbLinDamp", &rb.linearDamping, 0.005f, 0.0f, 1.0f, "%.3f");
        drawPropertyLabel("Angular Damping");
        changed |= ImGui::DragFloat("##RbAngDamp", &rb.angularDamping, 0.005f, 0.0f, 1.0f, "%.3f");

        changed |= drawVec3Control("Velocity", glm::value_ptr(rb.linearVelocity), 0.0f, 0.1f);

        if (changed) {
            // Wake the body so the edit (especially velocity) survives the next
            // tick - otherwise PhysicsSystem zeroes a sleeping body's velocity.
            // Applied to the live component before the card pushes the undo
            // command, so the wake is captured in the command's "after" value.
            rb.sleeping = false;
            rb.sleepTimer = 0.0f;
        }
        return changed;
    });
}

void InspectorPanel::drawColliderSection(Scene& scene, ResourceManager& resources, EditorState& state, EntityId id) {
    editComponentCard<Collider>(scene, state, id, "Collider", ACCENT_COLLIDER,
                                "Edit Collider", "Remove Collider",
                                [&](Collider& col) {
        bool changed = false;

        // A collider is a set of boxes. A single box is editable here; a
        // mesh-fitted compound shows its box count (rebuild it via Fit to Mesh).
        if (col.parts.size() == 1) {
            changed |= drawVec3Control("Half Extents",
                glm::value_ptr(col.parts[0].halfExtents), 0.5f, 0.05f);
        } else {
            ImGui::TextDisabled("%zu boxes (mesh-fitted)", col.parts.size());
        }

        // Fit to Mesh: rebuild the collider from this entity's mesh. Detail 1 is
        // a single box (the scaled bounds); higher detail voxelizes the mesh into
        // a box compound that hugs its shape. The entity scale is baked in - the
        // solver ignores Transform scale.
        if (scene.has<Mesh>(id) && scene.get<Mesh>(id).mesh) {
            const auto& asset = resources.get(scene.get<Mesh>(id).mesh);
            if (Math::hasValidBounds(asset.boundsMin, asset.boundsMax)) {
                ImGui::Spacing();
                drawPropertyLabel("Detail");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderInt("##ColDetail", &state.colliderFitDetail, 1, COLLIDER_FIT_MAX_DETAIL);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1 = one box; higher = a tighter box compound (more boxes = heavier)");
                if (ImGui::Button("Fit to Mesh", ImVec2(-1.0f, 0.0f))) {
                    const glm::vec3 scale = scene.has<Transform>(id)
                        ? scene.get<Transform>(id).scale : glm::vec3(1.0f);
                    col.parts = fitBoxesToMesh(asset, state.colliderFitDetail, scale);
                    changed = true;
                }
            }
        }

        drawPropertyLabel("Trigger");  changed |= ImGui::Checkbox("##ColTrigger", &col.isTrigger);

        return changed;
    });
}

void InspectorPanel::drawCameraSection(Scene& scene, EditorState& state, EntityId id) {
    editComponentCard<Camera>(scene, state, id, "Camera", ACCENT_CAMERA, "Edit Camera", "Remove Camera",
                              [&](Camera& cam) {
        bool changed = false;

        drawPropertyLabel("Projection");
        changed |= drawEnumCombo("##CProj", cam.projection);

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
            EditorActions::setActiveCamera(scene, state, id, "Set Main Camera");
        }

        return changed;
    });
}

void InspectorPanel::drawAnimationSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Animation", ACCENT_ANIM, true, &remove);
    if (open) {
        auto& anim = scene.get<Animation>(id);
        // Undo snapshot. Only authoring edits (length, keyframes) push a command;
        // play/pause/stop/scrub never set `changed`, so they stay non-undoable.
        // The snapshot does include time/playing, so undoing an authoring edit
        // also restores the scrub position - acceptable since edits are normally
        // made while paused.
        const Animation before = anim;

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

        bool changed = false;

        // Explicit minimum length holds the clip open past the last keyframe
        // (0 = auto, derived from the keyframes). Folds into `duration` via
        // updateDuration() so the scrubber and playback see it immediately.
        drawPropertyLabel("Length");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##ALength", &anim.length, 0.02f, 0.0f, 100000.0f, "%.2f s  (0 = auto)")) {
            anim.length = std::max(0.0f, anim.length);  // same clamp as the Bottom panel
            anim.updateDuration();
            changed = true;
        }

        if (anim.duration > 0.0f) {
            ImGui::SetNextItemWidth(-1);
            char timeFmt[32];
            snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", anim.duration);
            ImGui::SliderFloat("##ATime", &anim.time, 0.0f, anim.duration, timeFmt);
        }

        // Read-only keyframe summary. The full editable keyframe editor (add /
        // remove / retime / easing per track) lives in the Bottom panel's track
        // editor; duplicating it here drifted out of sync, so the inspector now
        // shows only a per-track digest and points the user at that editor.
        ImGui::Spacing();
        ImGui::TextUnformatted("Keyframes");
        auto trackSummary = [](const char* label, size_t count, float dur) {
            ImGui::BulletText("%s: %zu key%s, %.2fs", label, count, count == 1 ? "" : "s", dur);
        };
        trackSummary("Position", anim.positionTrack.keyframeCount(), anim.positionTrack.getDuration());
        trackSummary("Rotation", anim.rotationTrack.keyframeCount(), anim.rotationTrack.getDuration());
        trackSummary("Scale",    anim.scaleTrack.keyframeCount(),    anim.scaleTrack.getDuration());
        ImGui::TextDisabled("Edit keyframes in Bottom > Animation.");

        if (changed) {
            state.commands.push(std::make_unique<ComponentEditCommand<Animation>>(id, before, anim, "Edit Animation"));
            state.markSceneDirty();
        }
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

void InspectorPanel::drawScriptSection(Scene& scene, EditorState& state, EntityId id) {
    bool remove = false;
    const bool open = beginComponentCard("Script", ACCENT_SCRIPT, true, &remove);
    if (open) {
        auto& sc = scene.get<ScriptComponent>(id);

        // Script edits are applied live (no undo command): a behavior list is
        // move-only, so it can't ride the value-copying command stack.
        int removeIndex = -1;
        for (size_t i = 0; i < sc.behaviors.size(); ++i) {
            Behavior* behavior = sc.behaviors[i].get();
            if (!behavior) continue;
            ImGui::PushID(static_cast<int>(i));

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(behavior->typeName());
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeIndex = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove behavior");

            BehaviorFieldInspector inspector;
            behavior->visitFields(inspector);
            if (inspector.changed) state.markSceneDirty();

            ImGui::PopID();
            if (i + 1 < sc.behaviors.size()) ImGui::Separator();
        }

        if (sc.behaviors.empty()) ImGui::TextDisabled("No behaviors attached.");

        ImGui::Spacing();
        if (ImGui::Button("+  Add Behavior", ImVec2(-1, 0))) ImGui::OpenPopup("##AddBehavior");
        if (ImGui::BeginPopup("##AddBehavior")) {
            const std::vector<std::string> names = BehaviorRegistry::get().names();
            if (names.empty()) ImGui::TextDisabled("No behaviors registered.");
            for (const std::string& name : names) {
                if (ImGui::MenuItem(name.c_str())) {
                    if (auto behavior = BehaviorRegistry::get().create(name)) {
                        sc.behaviors.push_back(std::move(behavior));
                        state.markSceneDirty();
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (removeIndex >= 0) {
            sc.behaviors.erase(sc.behaviors.begin() + removeIndex);
            state.markSceneDirty();
        }
    }
    endComponentCard();
    if (remove) {
        scene.remove<ScriptComponent>(Entity{id});
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
                state.selectEntity(h.parent);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Unparent")) {
                EditorActions::reparentKeepingWorld(scene, state, id, EntityId{}, "Unparent");
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
                        state.selectEntity(child);
                    }
                });
            }
        }
    }
    endComponentCard();
}

} // namespace Engine
