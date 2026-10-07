#include "panels/inspector_panel.h"

#include <algorithm>
#include <climits>
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
#include <misc/cpp/imgui_stdlib.h>
#include <imgui.h>
#include <glm/glm.hpp>

#include "core/clock.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/audio/audio_listener.h"
#include "ecs/component/audio/audio_source.h"
#include "ecs/component/core/missing_assets.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/decal.h"
#include "ecs/component/render/irradiance_volume.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/particle_emitter.h"
#include "system/particle/live_particles.h"
#include "ecs/component/render/reflection_probe.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/environment.h"
#include "command/component_edit.h"
#include "editor_actions.h"
#include "command/editor_commands.h"
#include "core/system.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "editor_context.h"
#include "editor_state.h"
#include "resource/asset/material_asset.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"
#include "ui/field_label.h"
#include "command/prefab_overrides.h"
#include "resource/generate/light_generators.h"
#include "cook/lod_generator.h"
#include "io/asset/asset_library.h"
#include "io/project_paths.h"
#include "io/scene/scene_serializer.h"
#include "session/scene_io_controller.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "system/animation/animation_system.h"
#include "system/audio/audio_system.h"
#include "system/physics/authoring/collider_fit.h"
#include "system/physics/authoring/mesh_collider.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/joint.h"
#include "ecs/hierarchy_operations.h"
#include "system/script/behavior.h"
#include "system/script/behavior_field_visitor.h"
#include "system/script/behavior_registry.h"
#include "system/script/script_component.h"
#include "system/visibility/visibility.h"
#include "ui/audition_transport.h"
#include "core/math/bounds.h"
#include "net/wire/schema.h"
#include "net/replication/silence.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief A component's heading, accent and history-entry text, from one row.
 *
 * The labels are built from the heading as `static inline` strings: commands hold their
 * `const char*`, so they must live as long as the program.
 */
template <typename T>
struct CardInfo;

#define VKM_INSPECTOR_CARD_INFO(Component, method, title, accent)              \
    template <> struct CardInfo<Component> {                                   \
        static constexpr const char* TITLE = title;                            \
        static const ImVec4& accentColor() { return EditorStyle::Accent::accent; } \
        static inline const std::string ADD    = std::string("Add ")    + title; \
        static inline const std::string EDIT   = std::string("Edit ")   + title; \
        static inline const std::string REMOVE = std::string("Remove ") + title; \
    };
VKM_INSPECTOR_CARDS(VKM_INSPECTOR_CARD_INFO)
#undef VKM_INSPECTOR_CARD_INFO

// Scale from the collider's entity down to the mesh node: an import's child carries its own
// unit fix-up. Local scales are multiplied, not decomposed, so mirroring keeps its sign
// (an import chain has no rotation to approximate).
glm::vec3 meshScaleRelativeTo(const Scene& scene, EntityId collider, EntityId meshNode) {
    glm::vec3 scale(1.0f);
    HierarchyOperations::forSelfAndAncestors(scene, meshNode, [&](EntityId at) {
        if (const Transform* local = scene.tryGet<Transform>(at)) scale *= local->scale;
        return at != collider;
    });
    return scale;
}

// Touching clip planes are degenerate: glm::perspective divides by (zFar - zNear) and the
// cluster pass takes log(zFar / zNear).
constexpr float CLIP_PLANE_SEPARATION = 0.001f;

// Edits a Behavior*'s authored fields through the visitor serialization uses; rows are
// labelled fieldLabel(name), the name staying the saved key.
class BehaviorFieldInspector : public BehaviorFieldVisitor {
    public:
        explicit BehaviorFieldInspector(InspectorPanel::BehaviorEulers& eulers) : m_eulers(eulers) {}
        ~BehaviorFieldInspector() override = default;

        BehaviorFieldInspector(const BehaviorFieldInspector& other) = delete;
        BehaviorFieldInspector& operator=(const BehaviorFieldInspector& other) = delete;

        BehaviorFieldInspector(BehaviorFieldInspector && other) = delete;
        BehaviorFieldInspector& operator=(BehaviorFieldInspector && other) = delete;

    public:
        void field(const char* name, float& v) override {
            m_changed |= propRow(label(name), nullptr, [&] { return ImGui::DragFloat("##v", &v, 0.1f); });
        }
        void field(const char* name, int& v) override {
            m_changed |= propRow(label(name), nullptr, [&] { return ImGui::DragInt("##v", &v); });
        }
        void field(const char* name, bool& v) override {
            m_changed |= propRow(label(name), nullptr, [&] { return ImGui::Checkbox("##v", &v); });
        }
        void field(const char* name, glm::vec2& v) override {
            m_changed |= propRow(
                label(name),
                nullptr,
                [&] { return ImGui::DragFloat2("##v", glm::value_ptr(v), 0.1f); }
            );
        }
        void field(const char* name, glm::vec3& v) override {
            if (namesAColor(name)) {
                m_changed |= propColor3(label(name), glm::value_ptr(v));
                return;
            }
            m_changed |= propRow(
                label(name),
                nullptr,
                [&] { return ImGui::DragFloat3("##v", glm::value_ptr(v), 0.1f); }
            );
        }
        void field(const char* name, glm::vec4& v) override {
            if (namesAColor(name)) {
                m_changed |= propColor4(label(name), glm::value_ptr(v));
                return;
            }
            m_changed |= propRow(
                label(name),
                nullptr,
                [&] { return ImGui::DragFloat4("##v", glm::value_ptr(v), 0.1f); }
            );
        }
        // A cache per field, so a typed axis is not re-derived and snapped next frame.
        void field(const char* name, glm::quat& v) override {
            EulerCache<int>& euler = m_eulers[&v];
            euler.sync(0, v);
            if (propDrag3(label(name), euler.degrees(), 0.5f, 0.0f, 0.0f, "%.1f")) {
                v = euler.toQuat();
                m_changed = true;
            }
        }
        void field(const char* name, std::string& v) override {
            m_changed |= propRow(label(name), nullptr, [&] { return ImGui::InputText("##v", &v); });
        }

        void enumField(const char* name, int& index, const char* const* names, std::size_t count) override {
            m_changed |= propRow(
                label(name),
                nullptr,
                [&] { return comboList("##v", &index, names, static_cast<int>(count)); }
            );
        }

        // Lists the project's library, not what is loaded (unlike pickAsset); the name goes into
        // the scene's assets block on save, which is what loads it.
        void assetField(const char* name, std::string& assetName, AssetType type) override {
            drawPropertyLabel(label(name));
            ImGui::PushID(name);
            if (beginCombo("##v", assetName.empty() ? "(none)" : assetName.c_str())) {
                static char s_assetFilter[48] = {};
                popupSearchField("##assetFilter", s_assetFilter, sizeof(s_assetFilter));

                if (ImGui::Selectable("(none)", assetName.empty())) {
                    assetName.clear();
                    m_changed = true;
                }
                for (const std::string& candidate : AssetLibrary::get().namesOf(type)) {
                    if (!matchesFilter(candidate.c_str(), s_assetFilter)) continue;
                    if (ImGui::Selectable(candidate.c_str(), candidate == assetName)) {
                        assetName = candidate;
                        m_changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            // Not in the library (deleted, or from another project): the next load fails.
            if (!assetName.empty() && !AssetLibrary::get().find(type, assetName)) {
                ImGui::TextColored(EditorStyle::DANGER, "Not in this project's library.");
            }
            ImGui::PopID();
        }

        // A tree node whose ID scope keeps sibling structs' fields apart; endStruct/TreePop
        // runs only when beginStruct returned true.
        bool beginStruct(const char* name) override {
            return ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_DefaultOpen, "%s", label(name));
        }
        void endStruct() override { ImGui::TreePop(); }

        bool changed() const { return m_changed; }

    private:
        /**
         * @brief fieldLabel(@p name), valid until the next call.
         *
         * @param name The field's name.
         * @return Its label, held by this visitor.
         */
        const char* label(const char* name) {
            m_label = fieldLabel(name);
            return m_label.c_str();
        }

    private:
        InspectorPanel::BehaviorEulers& m_eulers;
        std::string                     m_label;
        bool                            m_changed = false;
};

/**
 * @brief Pick an asset the component names by string rather than by handle.
 *
 * UIText's font: engine fonts have no AssetType, so the component carries a name. An
 * unloaded current value is still offered, so opening the card does not reset it.
 *
 * @tparam Asset Asset type to list.
 * @param comboId ImGui id for the combo.
 * @param label Row label.
 * @param resources Graph to list from.
 * @param name Written when a row is chosen.
 * @param tooltip Row tooltip, or null.
 * @return Whether the field changed.
 */
template <typename Asset>
bool pickAssetByName(
    const char* comboId,
    const char* label,
    const ResourceManager& resources,
    std::string& name,
    const char* tooltip = nullptr
) {
    drawPropertyLabel(label);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    ImGui::SetNextItemWidth(-1.0f);
    if (!beginCombo(comboId, name.empty() ? "(none)" : name.c_str())) return false;

    bool picked  = false;
    bool listed  = false;
    resources.forEachOfType<Asset>([&](Handle<Asset>, const Asset& asset) {
        if (asset.isHidden()) return;
        listed = listed || asset.name() == name;
        if (ImGui::Selectable(asset.name().c_str(), asset.name() == name)) {
            name   = asset.name();
            picked = true;
        }
    });

    // An unloaded name, shown last and marked so it does not read as a choice.
    if (!name.empty() && !listed) {
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
        ImGui::Selectable((name + "  (not loaded)").c_str(), true);
        ImGui::PopStyleColor();
    }

    ImGui::EndCombo();
    return picked;
}

// Pick which loaded Asset a handle points at; the list is snapshotted for ImGuiListClipper.
// Returns true if the selection changed.
template <typename Asset, typename Handle>
bool pickAsset(const char* comboId, const char* label, ResourceManager& resources, Handle& currentHandle) {
    const Asset* current = resources.tryGet(currentHandle);
    const std::string cur = current ? current->name() : std::string("(none)");
    drawPropertyLabel(label);
    ImGui::SetNextItemWidth(-1.0f);
    if (!beginCombo(comboId, cur.empty() ? "(unnamed)" : cur.c_str()))
        return false;

    std::vector<std::pair<Handle, const Asset*>> rows;
    resources.forEachOfType<Asset>([&](Handle h, const Asset& a) {
        if (a.isHidden()) return;
        rows.emplace_back(h, &a);
    });

    bool picked = false;

    if (ImGui::Selectable("(none)", !currentHandle)) {
        currentHandle = Handle{};
        picked = true;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& [h, a] = rows[i];
            ImGui::PushID(static_cast<int>(h.id()));
            const bool sel = currentHandle == h;
            if (ImGui::Selectable(a->name().empty() ? "(unnamed)" : a->name().c_str(), sel)) {
                currentHandle = h;
                picked = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndCombo();
    return picked;
}

// Pick the joint of @p skeleton (the rig the Animator above the socket resolved). A null
// skeleton shows the stored name disabled, so an unloaded rig's socket shows what it awaits.
bool pickBone(const char* comboId, const SkeletonAsset* skeleton, std::string& bone) {
    drawPropertyLabel("Bone");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(skeleton == nullptr);

    bool picked = false;
    if (beginCombo(comboId, bone.empty() ? "(none)" : bone.c_str())) {
        // BeginDisabled does not close an already-open popup, and the rig can vanish under it.
        if (!skeleton) {
            ImGui::CloseCurrentPopup();
        } else {
            static char s_boneFilter[48] = {};
            popupSearchField("##boneFilter", s_boneFilter, sizeof(s_boneFilter));

            if (ImGui::Selectable("(none)", bone.empty())) {
                bone.clear();
                picked = true;
            }

            // Indented by depth (parent < index makes it one pass); not when filtered.
            const bool filtered = s_boneFilter[0] != '\0';
            std::vector<int> depth(skeleton->bones.size(), 0);
            std::vector<std::pair<const std::string*, int>> rows;
            for (size_t i = 0; i < skeleton->bones.size(); ++i) {
                const Bone& joint = skeleton->bones[i];
                depth[i] = joint.parent < 0 ? 0 : depth[joint.parent] + 1;
                if (matchesFilter(joint.name.c_str(), s_boneFilter)) {
                    rows.emplace_back(&joint.name, filtered ? 0 : depth[i]);
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rows.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& [name, indent] = rows[i];
                    ImGui::PushID(i);
                    const std::string label = std::string(static_cast<size_t>(indent) * 2, ' ') + *name;
                    if (ImGui::Selectable(label.c_str(), *name == bone)) {
                        bone   = *name;
                        picked = true;
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::EndDisabled();
    return picked;
}

/**
 * @brief A row per field of one component this prefab instance overrides, each with a revert.
 *
 * Nothing outside an instance. Drawn before the fields, so a revert shows its restored value.
 *
 * @param scene     Scene holding the entity; a revert patches it.
 * @param resources Resolves the prefab's asset names for a revert.
 * @param state     Receives a revert's step.
 * @param id        Entity whose overrides are listed.
 * @param component Component key, as SceneSerializer writes it.
 * @param rowLabel  Replaces the field key when the widget is named otherwise (the name box,
 *                  key "value").
 */
void drawOverrideRows(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id,
    const char* component,
    const char* rowLabel = nullptr
) {
    const std::vector<std::string> fields =
        PrefabOverrides::overriddenFields(scene, id, component);
    if (fields.empty()) return;

    std::string revert;
    ImGui::TextColored(EditorStyle::Accent::PREFAB, "Overridden by this instance");
    for (const std::string& field : fields) {
        drawPropertyLabel(rowLabel ? rowLabel : field.c_str());
        ImGui::PushID(field.c_str());
        if (ImGui::Button("Revert to prefab", ImVec2(-1.0f, 0.0f))) revert = field;
        ImGui::PopID();
    }
    ImGui::Separator();

    if (!revert.empty()) PrefabOverrides::revert(scene, resources, state, id, component, revert);
}

// Ragdoll owns a subtree of bodies its own Clear removes; a header remove would strand them.
template <typename T>
constexpr bool CARD_OWNS_MORE_THAN_ITSELF = std::is_same_v<T, Ragdoll>;

// Scaffold for a removable, value-edited card. `drawFields` gets the live component and
// returns whether it edited it. The edit is pushed before endComponentCard, the remove after.
template <typename T, typename DrawFields>
void editComponentCard(EditorContext& ec, EntityId id, DrawFields drawFields) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState&     state     = ec.state;
    bool remove = false;
    const bool open = beginComponentCard(
        CardInfo<T>::TITLE,
        CardInfo<T>::accentColor(),
        true,
        CARD_OWNS_MORE_THAN_ITSELF<T> ? nullptr : &remove
    );
    if (open) {
        drawOverrideRows(scene, resources, state, id, PrefabOverrides::COMPONENT_KEY<T>);

        auto& component = scene.get<T>(id);
        const T before = component;  // pre-edit value, for undo
        const bool changed = drawFields(component);
        if (changed) {
            pushEdit<T>(scene, resources, state, id, before, component, CardInfo<T>::EDIT.c_str());
        }
    }
    endComponentCard();
    if (remove) {
        T snap = scene.get<T>(id);
        scene.remove<T>(id);
        auto removal = std::make_unique<RemoveComponentCommand<T>>(
            id,
            std::move(snap),
            CardInfo<T>::REMOVE.c_str()
        );
        state.pushStep(std::move(removal));
        PrefabOverrides::warnComponentIsPrefabs(
            scene,
            state,
            id,
            CardInfo<T>::TITLE,
            "comes back from the prefab on the next load"
        );
    }
}

// editComponentCard for a value the Scene holds outside any entity: a World card, never removed.
template <typename T, typename DrawFields>
void editWorldCard(
    EditorState& state,
    const char* title,
    const ImVec4& accent,
    T& value,
    const char* label,
    DrawFields drawFields
) {
    if (beginComponentCard(title, accent, true)) {
        const T before = value;
        if (drawFields(value)) {
            state.pushStep(std::make_unique<SceneValueEditCommand<T>>(before, value, label));
        }
    }
    endComponentCard();
}

/**
 * @brief The line every UI content card owes when its entity carries no UIElement.
 *
 * Their rect is the UIElement's; without one UISystem returns from resolveElement first.
 *
 * @param scene Scene holding the entity.
 * @param id    Entity whose card is being drawn.
 */
void warnNoUIElement(const Scene& scene, EntityId id) {
    if (scene.has<UIElement>(id)) return;
    ImGui::TextColored(EditorStyle::DANGER, "No UI Element: nothing to give it a rect");
    ImGui::TextDisabled("Nothing draws for it until one is added.");
}

// The first capsule's radius, or 0: a character wears one, and a compound fit is all boxes.
float capsuleRadiusOf(const Scene& scene, EntityId id) {
    const Collider* collider = scene.tryGet<Collider>(id);
    if (!collider) return 0.0f;
    for (const ColliderPart& part : collider->parts)
        if (part.shape == ColliderShape::Capsule) return part.radius;
    return 0.0f;
}
} // namespace

void InspectorPanel::draw(EditorContext& ec, const SceneIOController& sceneIO) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;
    m_sessionPlaying = sceneIO.isPlaying();

    if (m_previewVoice != 0 && state.selectedEntity != m_previewOwner) {
        ec.audioSystem.device().stopVoice(m_previewVoice);
        m_previewVoice = 0;
    }

    if (state.selection.size() > 1) {
        ImGui::TextColored(EditorStyle::ACCENT, "%zu entities selected", state.selection.size());
        ImGui::TextDisabled("Editing the active entity below.");
        ImGui::Separator();
    }

    const bool haveEntity = state.selectedEntity && ctx.scene.isAlive(state.selectedEntity);
    if (!haveEntity) {
        if (state.worldSelected) drawWorldInspector(ec);
        else                     drawEmptySelectionState(ec);
        return;
    }

    Scene& scene = ctx.scene;
    EntityId id  = state.selectedEntity;

    drawIdentityHeader(scene, ctx.resources, state, id);

    // Never pruned: an undo can empty the field again; see docs/reference/io.md.
    const std::vector<MissingAssetRef> missing =
        SceneSerializer::unresolvedRefs(scene, ctx.resources, id);
    if (!missing.empty()) {
        ImGui::TextColored(EditorStyle::DANGER, "%zu asset reference(s) here did not load:", missing.size());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        for (const MissingAssetRef& ref : missing) {
            ImGui::TextWrapped("  %s.%s  '%s'", ref.component.c_str(), ref.field.c_str(), ref.name.c_str());
        }
        ImGui::TextWrapped("Kept as written: the save puts them back rather than emptying them.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawPrefabSection(ec, id);

#define VKM_INSPECTOR_CARD_DRAW(Component, method, title, accent) \
    if (scene.has<Component>(id)) method(ec, id);
    VKM_INSPECTOR_CARDS(VKM_INSPECTOR_CARD_DRAW)
#undef VKM_INSPECTOR_CARD_DRAW

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    drawAddComponentMenu(scene, state, id);
}

void InspectorPanel::drawEmptySelectionState(EditorContext& ec) {
    FrameContext& ctx   = ec.frame;
    EditorState&  state = ec.state;

    emptyStateHeading(
        EditorIcon::Select,
        "No entity selected",
        "Pick one in the Hierarchy, or click in the viewport.",
        1
    );

    const float btnW = EditorStyle::px(180.0f);
    centreNextItem(btnW);
    if (ImGui::Button("+  Create Entity", ImVec2(btnW, 0.0f)))
        ImGui::OpenPopup("##EmptyCreate");
    if (ImGui::BeginPopup("##EmptyCreate")) {
        EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, state);
        ImGui::EndPopup();
    }
}

namespace {

// What the other end sees of this entity, when it carries replicated state.
void drawWireIdentity(const Scene& scene, EntityId id) {
    const NetSchema& schema = NetSchema::get();
    if (schema.size() == 0) return;

    std::string carried;
    for (const NetType& type : schema.types()) {
        if (!type.has(scene, id)) continue;
        if (!carried.empty()) carried += ", ";
        carried += type.name;
    }
    if (carried.empty()) return;

    // The slot is the wire name and survives save and load.
    const NetSilence silence = netSilence(scene, id);
    if (silence != NetSilence::None) {
        static constexpr const char* WHY[] = {
            "",
            "inside a prefab instance",
            "the animation places it",
            "it cannot move"
        };
        static_assert(
            std::size(WHY) == static_cast<size_t>(NetSilence::Count),
            "every reason an entity is off the wire needs a word for the author"
        );

        ImGui::TextColored(EditorStyle::WARNING, "Not on the wire: %s.", WHY[static_cast<size_t>(silence)]);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", toString(silence));
        return;
    }
    ImGui::TextDisabled("Wire slot %u  -  %s", id.slot(), carried.c_str());
}

} // namespace

void InspectorPanel::drawIdentityHeader(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId id
) {
    // Name is added only on user action, so looking never mutates the scene.
    const float ih = ImGui::GetFrameHeight();
    inlineIcon(entityIconKind(scene, id), ih, ImGui::GetColorU32(EditorStyle::ACCENT));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("#%u", id.slot());
    ImGui::SameLine();

    if (Name* name = scene.tryGet<Name>(id)) {
        const Name before = *name;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##Name", name->value, sizeof(name->value))) {
            // tryMerge coalesces the keystrokes into one undo step.
            pushEdit<Name>(scene, resources, state, id, before, *name, "Rename");
        }
        drawOverrideRows(scene, resources, state, id, PrefabOverrides::COMPONENT_KEY<Name>, "Name");
    } else {
        char fallback[64];
        getEntityDisplayName(scene, id, fallback, sizeof(fallback));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fallback);
        ImGui::SameLine();
        if (ImGui::SmallButton("+##addname")) {
            EditorActions::addComponent(scene, state, id, makeName(fallback), "Name", "Add Name");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a Name component to rename this entity");
    }

    drawWireIdentity(scene, id);
}

void InspectorPanel::drawUICanvasSection(EditorContext& ec, EntityId id) {
    editComponentCard<UICanvas>(ec, id, [&](UICanvas& c) {
        bool changed = false;
        changed |= propEnumCombo("Scale Mode", c.scaleMode);
        changed |= propDrag(
            "Reference Height",
            &c.referenceHeight,
            1.0f,
            1.0f,
            8192.0f,
            "%.0f",
            "Authoring height; ScaleWithHeight scales the layout against it."
        );
        changed |= propDragInt(
            "Sort Order",
            &c.sortOrder,
            1.0f,
            -1000,
            1000,
            "Higher draws on top across canvases."
        );
        changed |= propCheckbox("Visible", &c.visible);
        return changed;
    });
}

void InspectorPanel::drawUIElementSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<UIElement>(ec, id, [&](UIElement& e) {
        bool changed = false;
        changed |= propRow("Anchor", "Parent anchor point, 0..1 (top-left to bottom-right).", [&] {
            return ImGui::DragFloat2("##v", glm::value_ptr(e.anchor), 0.005f, 0.0f, 1.0f, "%.3f", PROP_CLAMP);
        });
        changed |= propRow("Pivot", "Element pivot, 0..1; the point placed at the anchor.", [&] {
            return ImGui::DragFloat2("##v", glm::value_ptr(e.pivot), 0.005f, 0.0f, 1.0f, "%.3f", PROP_CLAMP);
        });
        changed |= propRow("Position", "Offset from the anchor, in reference pixels.", [&] {
            return ImGui::DragFloat2("##v", glm::value_ptr(e.position), 0.5f, 0.0f, 0.0f, "%.1f");
        });
        const char* sizeTooltip =
            "Element size, in reference pixels, added to Relative Size's "
            "share of the parent. Negative only means something beside a "
            "Relative Size: it is the margin taken off the share.";
        changed |= propRow("Size", sizeTooltip, [&] {
            return ImGui::DragFloat2(
                "##v",
                glm::value_ptr(e.size),
                0.5f,
                -8192.0f,
                8192.0f,
                "%.1f",
                PROP_CLAMP
            );
        });
        const char* relativeSizeTooltip =
            "Share of the parent's size, 0..1, added to Size. "
            "1, 1 with a Size of 0 fills the parent at any resolution.";
        changed |= propRow("Relative Size", relativeSizeTooltip, [&] {
            return ImGui::DragFloat2(
                "##v",
                glm::value_ptr(e.relativeSize),
                0.005f,
                0.0f,
                1.0f,
                "%.3f",
                PROP_CLAMP
            );
        });
        changed |= propCheckbox("Visible", &e.visible, "Hides this element and its whole subtree.");
        const char* blocksPointerTooltip =
            "Stops a click reaching what is behind. Off for a decorative "
            "overlay meant to be clicked through. Only consulted on an "
            "element that draws - an image or a button.";
        changed |= propCheckbox("Blocks pointer", &e.blocksPointer, blocksPointerTooltip);
        const char* clipChildrenTooltip =
            "Bounds what descendants draw, and what the pointer can reach, "
            "to this rect. A UI Scroll clips whether or not this is set.";
        changed |= propCheckbox("Clip children", &e.clipChildren, clipChildrenTooltip);

        if (!hasCanvasAncestor(scene, id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No UI Canvas above this: nothing draws");
            ImGui::TextDisabled("Drag this entity onto a canvas in the Hierarchy.");
        }
        return changed;
    });
}

namespace {

// Corners, edge and fade: shared by the UI Image and UI Button cards, one UIShape quad each.
bool drawShapeRows(UIShape& shape) {
    bool changed = false;
    const char* cornerTooltip =
        "Corner rounding in reference pixels. Past half the shorter "
        "side it stops growing, so a square becomes a disc.";
    changed |= propDrag("Corner Radius", &shape.cornerRadius, 0.5f, 0.0f, 4096.0f, "%.1f", cornerTooltip);
    changed |= propDrag(
        "Border",
        &shape.borderWidth,
        0.25f,
        0.0f,
        512.0f,
        "%.1f",
        "Edge stroke drawn inside the rect, in reference pixels; 0 is none."
    );
    if (shape.borderWidth > 0.0f) {
        changed |= propColor4("Border Color", glm::value_ptr(shape.borderColor));
    }
    const char* gradientTooltip =
        "Fade the fill from its colour at the top to Bottom Color "
        "at the bottom edge.";
    changed |= propCheckbox("Gradient", &shape.gradient, gradientTooltip);
    if (shape.gradient) {
        changed |= propColor4("Bottom Color", glm::value_ptr(shape.bottomColor));
    }
    return changed;
}

} // namespace

void InspectorPanel::drawUIImageSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    ResourceManager& resources = ec.frame.resources;

    editComponentCard<UIImage>(ec, id, [&](UIImage& i) {
        bool changed = propColor4("Color", glm::value_ptr(i.color));
        changed |= pickAsset<TextureAsset>("##UIImageTexPick", "Texture", resources, i.texture);
        changed |= drawShapeRows(i.shape);
        warnNoUIElement(scene, id);
        return changed;
    });
}

void InspectorPanel::drawUITextSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<UIText>(ec, id, [&](UIText& t) {
        bool changed = false;
        changed |= propString("Text", t.text);
        changed |= pickAssetByName<FontAsset>(
            "##FontPick",
            "Font",
            resources,
            t.font,
            "The baked SDF font this text is drawn with."
        );
        changed |= propDrag(
            "Size",
            &t.pixelSize,
            0.5f,
            1.0f,
            512.0f,
            "%.0f",
            "Text height in reference pixels."
        );
        changed |= propEnumCombo("Align", t.align);
        changed |= propEnumCombo("V Align", t.valign);
        const char* wrapTooltip =
            "Break a line too wide for the element onto the next one. A "
            "newline in the text always breaks, wrap or not.";
        changed |= propCheckbox("Wrap", &t.wrap, wrapTooltip);
        changed |= propColor4("Color", glm::value_ptr(t.color));

        warnNoUIElement(scene, id);

        // An unresolved font draws nothing, like a hidden element; the combo marks it only when open.
        if (!resources.findByName<FontAsset>(t.font)) {
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
            ImGui::TextWrapped("No font named '%s' is loaded", t.font.empty() ? "" : t.font.c_str());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("Nothing draws until the name matches one.");
        }
        return changed;
    });
}

void InspectorPanel::drawUIButtonSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<UIButton>(ec, id, [&](UIButton& b) {
        bool changed = false;
        changed |= propString(
            "Event Id",
            b.eventId,
            "Identifier the UIClickEvent carries when this button fires."
        );
        changed |= propCheckbox("Interactable", &b.interactable);
        changed |= propColor4("Normal", glm::value_ptr(b.normalColor));
        changed |= propColor4("Hover", glm::value_ptr(b.hoverColor));
        changed |= propColor4("Pressed", glm::value_ptr(b.pressedColor));
        changed |= propColor4("Disabled", glm::value_ptr(b.disabledColor));
        changed |= drawShapeRows(b.shape);

        warnNoUIElement(scene, id);
        return changed;
    });
}

void InspectorPanel::drawUIScrollSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<UIScroll>(ec, id, [&](UIScroll& s) {
        bool changed = false;
        const glm::vec2 room = s.range();
        changed |= propRow("Offset", "How far the content has scrolled, in reference pixels.", [&] {
            return ImGui::DragFloat2("##v", glm::value_ptr(s.offset), 1.0f, 0.0f, 0.0f, "%.0f");
        });
        changed |= propDrag(
            "Wheel Step",
            &s.wheelStep,
            1.0f,
            1.0f,
            500.0f,
            "%.0f",
            "Reference pixels the content moves per wheel notch."
        );

        warnNoUIElement(scene, id);

        // Measured by the layout walk, not authored: shown, not edited.
        ImGui::TextDisabled(
            "Content %.0f x %.0f, view %.0f x %.0f",
            s.contentSize.x,
            s.contentSize.y,
            s.viewSize.x,
            s.viewSize.y
        );
        if (room.x <= 0.0f && room.y <= 0.0f) {
            ImGui::TextDisabled("The content fits, so there is nothing to scroll.");
        }
        return changed;
    });
}

void InspectorPanel::drawAddComponentMenu(Scene& scene, EditorState& state, EntityId id) {
    // Inside an instance a component lives only in the prefab; the button stays, as saving the
    // prefab back authors one (see PrefabOverrides::warnComponentIsPrefabs).
    const bool inInstance = PrefabOverrides::instanceRoot(scene, id) != EntityId{};
    if (inInstance) {
        ImGui::TextWrapped(
            "Components on an instance belong to the prefab. Save as Prefab "
            "keeps what you add here; saving the scene does not."
        );
    }

    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    const bool clicked = ImGui::Button("+  Add Component", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (clicked) ImGui::OpenPopup("##AddComp");

    if (ImGui::BeginPopup("##AddComp")) {
        sectionLabel("Add Component");
        static char s_componentFilter[48] = {};
        popupSearchField(
            "##compFilter",
            s_componentFilter,
            sizeof(s_componentFilter),
            EditorStyle::px(200.0f)
        );
        // Headings, not submenus, so filter matches are reachable; a heading waits for its first item.
        const char* pendingSection = nullptr;
        bool        sectionDrawn   = false;
        const auto section = [&](const char* label) { pendingSection = label; };
        const auto drawPendingSection = [&] {
            if (pendingSection == nullptr) return;
            if (sectionDrawn) ImGui::Spacing();
            sectionLabel(pendingSection);
            pendingSection = nullptr;
            sectionDrawn   = true;
        };

        auto addItem = [&](auto value) {
            using T = decltype(value);
            const char* label = CardInfo<T>::TITLE;
            if (scene.has<T>(id)) return;
            if (!matchesFilter(label, s_componentFilter)) return;

            drawPendingSection();
            if (!ImGui::MenuItem(label)) return;

            // The warning above is hidden by the open menu, so the add repeats it.
            EditorActions::addComponent(scene, state, id, std::move(value), label, CardInfo<T>::ADD.c_str());
        };

        // Mirrors ecs/component/'s folders, plus Script from system/script/. No Prefab (only saving
        // one writes it); Name is the + beside the entity title.
        section("Core");
        addItem(Transform{});

        section("Render");
        addItem(Mesh{});
        addItem(generateLight(LightType::Point));
        Camera cam;
        cam.active = false;
        addItem(cam);
        addItem(Decal{});
        addItem(LOD{});
        addItem(ParticleEmitter{});
        addItem(ReflectionProbe{});
        addItem(IrradianceVolume{});

        section("Animation");
        addItem(Animation{});
        addItem(Animator{});
        addItem(BoneSocket{});

        section("Audio");
        addItem(AudioSource{});
        addItem(AudioListener{});

        section("Physics");
        addItem(Rigidbody{});
        addItem(Collider{});
        addItem(CharacterController{});
        addItem(Joint{});
        addItem(Ragdoll{});

        section("UI");
        addItem(UICanvas{});
        addItem(UIElement{});
        addItem(UIImage{});
        addItem(UIText{});
        addItem(UIButton{});
        addItem(UIScroll{});

        // Move-only, so not AddComponentCommand: ScriptEditCommand holds it serialized.
        section("Script");
        if (!scene.has<ScriptComponent>(id)
            && matchesFilter(CardInfo<ScriptComponent>::TITLE, s_componentFilter)) {
            drawPendingSection();
            if (ImGui::MenuItem(CardInfo<ScriptComponent>::TITLE)) {
                scene.add(id, ScriptComponent{});
                auto add = std::make_unique<ScriptEditCommand>(
                    id,
                    std::string{},
                    ScriptEditCommand::capture(scene, id),
                    CardInfo<ScriptComponent>::ADD.c_str()
                );
                state.pushStep(std::move(add));
                PrefabOverrides::warnComponentIsPrefabs(
                    scene,
                    state,
                    id,
                    CardInfo<ScriptComponent>::TITLE,
                    "is not stored in the scene"
                );
            }
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::drawPrefabSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    const EntityId root = PrefabOverrides::instanceRoot(scene, id);
    if (!root) return;

    const bool open = beginComponentCard("Prefab Instance", EditorStyle::Accent::PREFAB, true);
    if (open) {
        const PrefabInstance& instance = scene.get<PrefabInstance>(root);

        drawPropertyLabel("Source");
        ImGui::TextWrapped("%s", instance.source.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", instance.source.c_str());

        // Below the root, point to it: the overrides and the file live there.
        if (root != id) {
            drawPropertyLabel("Root");
            char rootName[64];
            getEntityDisplayName(scene, root, rootName, sizeof(rootName));
            if (ImGui::SmallButton(rootName)) state.selectEntity(root);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select the instance root");
        }

        const PrefabEntity* marker = scene.tryGet<PrefabEntity>(id);
        if (id == root && !marker) {
            // An expansion marks every entity it builds, so an unmarked root means the file did not
            // open. Checked before the scene-added-child branch.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped(
                "The prefab file could not be opened, so this instance is "
                "empty. Restore it and load the scene again - the reference "
                "and any overrides are kept until then."
            );
            ImGui::PopStyleColor();
        } else if (!marker) {
            // Unstorable: the scene saves the instance as a reference, and the prefab lacks it.
            ImGui::TextWrapped(
                "Added to the scene, not to the prefab - this entity is "
                "dropped when the scene is loaded again."
            );
        } else {
            const uint32_t uid = marker->uid;
            size_t here = 0;
            for (const PrefabOverride& o : instance.overrides) {
                if (o.uid == uid) ++here;
            }
            if (instance.overrides.empty()) {
                ImGui::TextDisabled("No overrides. Editing a field here makes one.");
            } else {
                ImGui::TextDisabled(
                    "%zu override(s) here, %zu in the instance.",
                    here,
                    instance.overrides.size()
                );
            }
        }
    }
    endComponentCard();
}

void InspectorPanel::drawTransformSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    // Transform cannot be removed.
    const bool open = beginComponentCard(
        CardInfo<Transform>::TITLE,
        CardInfo<Transform>::accentColor(),
        true
    );
    if (open) {
        drawOverrideRows(scene, resources, state, id, PrefabOverrides::COMPONENT_KEY<Transform>);

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
            // tryMerge collapses the drag into one undo step.
            pushEdit<Transform>(scene, resources, state, id, before, t, "Transform");
        }

        const Hierarchy* node = scene.tryGet<Hierarchy>(id);
        if (node && node->parent) {
            glm::mat4 worldMat = HierarchyOperations::computeWorldMatrix(scene, id);
            glm::vec3 worldPos(worldMat[3]);
            ImGui::TextDisabled("World: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y, worldPos.z);
        }

        // A socket rewrites this every frame from its bone; say where the lasting value lives.
        if (scene.has<BoneSocket>(id)) {
            ImGui::TextDisabled("Driven by Bone Socket - author Offset on that card.");
        }
    }
    endComponentCard();
}

void InspectorPanel::drawMeshSection(EditorContext& ec, EntityId id) {
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    editComponentCard<Mesh>(ec, id, [&](Mesh& mesh) {
        bool changed = false;

        changed |= propCheckbox("Visible", &mesh.visible);
        changed |= propCheckbox("Cast Shadow", &mesh.castShadows);

        if (const MeshAsset* held = resources.tryGet(mesh.mesh)) {
            const MeshAsset& asset = *held;
            ImGui::TextDisabled("%zu verts, %zu tris", asset.vertices.size(), asset.indices.size() / 3);
            if (asset.bounds().valid()) {
                glm::vec3 ext = asset.boundsMax - asset.boundsMin;
                ImGui::TextDisabled("Bounds: %.1f x %.1f x %.1f", ext.x, ext.y, ext.z);
            }
            // The rig resolved, not the name the mesh remembers.
            if (!asset.skin.empty()) {
                const SkeletonHandle rig = resources.findByName<SkeletonAsset>(asset.skeleton);
                if (rig) {
                    ImGui::TextDisabled(
                        "Skinned: %zu bones (%s)",
                        resources.get(rig).bones.size(),
                        asset.skeleton.c_str()
                    );
                } else {
                    ImGui::TextDisabled("Skinned: rig '%s' not loaded", asset.skeleton.c_str());
                }
            }
        } else {
            // VisibilitySystem skips an empty mesh handle (and, below, material): nothing draws.
            ImGui::TextColored(EditorStyle::DANGER, "No mesh: this entity draws nothing");
        }

        ImGui::Spacing();

        changed |= pickAsset<MeshAsset>("##MeshPick", "Mesh Asset", resources, mesh.mesh);
        changed |= pickAsset<MaterialAsset>("##MatPick", "Material Asset", resources, mesh.material);

        ImGui::Spacing();

        if (mesh.material) {
            const MaterialAsset& m = resources.get(mesh.material);
            ImGui::TextDisabled("Material: %s", m.name().empty() ? "(unnamed)" : m.name().c_str());
            const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button("Edit Material", ImVec2(bw, 0))) {
                state.openMaterial(mesh.material);
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate", ImVec2(bw, 0))) {
                if (MaterialHandle nh = EditorActions::duplicateMaterial(resources, mesh.material)) {
                    // The live component: the card scaffold pushes the step on `changed`.
                    mesh.material = nh;
                    state.openMaterial(nh);
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fork the material and edit the copy safely");
        } else {
            ImGui::TextColored(EditorStyle::DANGER, "No material: this entity draws nothing");
        }

        return changed;
    });
}

void InspectorPanel::drawLightSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    // With the procedural sky on, SkySystem writes this light's rotation, colour and intensity;
    // findKeyLight keeps the card and the system agreeing on which light.
    const bool skyDriven = scene.environment().sky.procedural && findKeyLight(scene) == id;

    editComponentCard<Light>(ec, id, [&](Light& light) {
        bool changed = false;

        if (skyDriven) {
            ImGui::TextWrapped(
                "Procedural Sky drives this light: its rotation, colour and "
                "intensity are written from World > Procedural Sky every "
                "frame, and are what the scene saves."
            );
            ImGui::Spacing();
        }

        changed |= propEnumCombo("Type", light.type);

        // Disabled, not hidden: still the light's values and the file's.
        ImGui::BeginDisabled(skyDriven);
        changed |= propColor3("Color", glm::value_ptr(light.color));

        // Generous: a sun needs hundreds, a studio light thousands, and PROP_CLAMP binds typed values.
        changed |= propDrag("Intensity", &light.intensity, 0.5f, 0.0f, 100000.0f, "%.2f");
        ImGui::EndDisabled();

        if (light.type != LightType::Directional)
            changed |= propDrag("Radius", &light.radius, 0.5f, 0.1f, 1000.0f, "%.1f");

        if (light.type == LightType::Spot) {
            if (propAngleDrag("Inner Cone", &light.innerConeAngle, 0.5f, 0.0f, 90.0f)) {
                changed = true;
            }
            if (propAngleDrag("Outer Cone", &light.outerConeAngle, 0.5f, 0.0f, 90.0f)) {
                changed = true;
            }
        }

        if (light.type == LightType::Rect) {
            changed |= propDrag("Width", &light.areaWidth, 0.05f, 0.01f, 100.0f, "%.2f");
            changed |= propDrag("Height", &light.areaHeight, 0.05f, 0.01f, 100.0f, "%.2f");
            changed |= propCheckbox("Two-sided", &light.twoSided);
        }
        if (light.type == LightType::Disk) {
            changed |= propDrag("Disk Radius", &light.areaRadius, 0.05f, 0.01f, 100.0f, "%.2f");
            changed |= propCheckbox("Two-sided", &light.twoSided);
        }
        changed |= propCheckbox("Shadows", &light.castShadows);
        if (light.castShadows) {
            const bool shadowed = light.type == LightType::Directional
                || light.type == LightType::Spot || light.type == LightType::Point;
            if (shadowed) {
                const char* biasTooltip =
                    "How far the shadow compare slides toward the light, in shadow texels - "
                    "more where the light grazes. Raise it against acne; too much detaches "
                    "a shadow from its caster";
                changed |= propDrag(
                    "Shadow Bias",
                    &light.shadowBias,
                    0.05f,
                    0.0f,
                    4.0f,
                    "%.2f texels",
                    biasTooltip
                );
                const char* normalBiasTooltip =
                    "How far a point moves off its surface before the shadow compare, in "
                    "shadow texels - none head-on, all of it where the light grazes. Raise "
                    "it against acne on curved surfaces; too much loses thin shadows";
                changed |= propDrag(
                    "Normal Bias",
                    &light.shadowNormalBias,
                    0.05f,
                    0.0f,
                    4.0f,
                    "%.2f texels",
                    normalBiasTooltip
                );
            }
            if (light.type == LightType::Directional)
                changed |= propDrag("Shadow Distance", &light.shadowDistance, 1.0f, 1.0f, 1000.0f, "%.1f");
        }
        // Source size, shadowed or not: it sizes the highlight as well as the penumbra.
        if (light.type == LightType::Directional) {
            float degrees = glm::degrees(light.sourceRadius);
            const char* sourceSizeTooltip =
                "Angular radius of the disc the light is seen as: how soft its "
                "shadows are, how wide its highlight, and how large the sky draws the "
                "sun. The real sun is 0.27";
            if (propDrag("Source Size", &degrees, 0.01f, 0.0f, 10.0f, "%.2f deg", sourceSizeTooltip)) {
                light.sourceRadius = glm::radians(degrees);
                changed = true;
            }
        } else if (light.type == LightType::Spot || light.type == LightType::Point) {
            const char* sourceRadiusTooltip =
                "Radius of the emitter: how wide its highlight is and how soft its "
                "shadows are, softest far from what casts them";
            changed |= propDrag(
                "Source Radius",
                &light.sourceRadius,
                0.005f,
                0.0f,
                2.0f,
                "%.3f m",
                sourceRadiusTooltip
            );
        }
        changed |= propCheckbox("Enabled", &light.enabled);

        return changed;
    });
}

void InspectorPanel::drawWorldInspector(EditorContext& ec) {
    EditorState& state = ec.state;
    Scene&       scene = ec.frame.scene;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("World");
    ImGui::SameLine();
    ImGui::TextDisabled(" Scene-global settings");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    editWorldCard(
        state,
        "Environment",
        EditorStyle::Accent::ENV,
        scene.environment(),
        "Edit Environment",
        [&](Environment& env) {
            bool changed = false;

            // Project-relative ("assets/envs/<file>.hdr"), as loadHDRImage resolves it.
            drawPropertyLabel("Skybox HDR");
            ImGui::TextUnformatted(env.sky.hdrPath.empty() ? "(none)" : env.sky.hdrPath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse...")) {
                AssetPicker::Options options;
                options.title      = "Pick Environment HDR";
                options.root       = ProjectPaths::envs();
                options.extensions = {".hdr"};
                m_envPicker.open(options);
            }
            std::string pickedHdr;
            if (m_envPicker.draw(pickedHdr)) {
                std::string rel = std::filesystem::path(pickedHdr).generic_string();
                if (rel != env.sky.hdrPath) {
                    env.sky.hdrPath = rel;
                    changed = true;
                }
            }

            changed |= propCheckbox("Show Skybox", &env.sky.showSkybox);
            changed |= propSlider(
                "Brightness",
                &env.sky.intensity,
                0.0f,
                3.0f,
                "%.2f",
                "Indirect (IBL) strength. Swapping the HDR re-bakes the IBL (a brief hitch)."
            );
            return changed;
        }
    );

    // Live: a changed value re-bakes the lighting at once, so a slow drag of one hitches each
    // frame; the sun dragged slowly is followed over frames instead.
    editWorldCard(
        state,
        "Procedural Sky",
        EditorStyle::Accent::ENV,
        scene.environment(),
        "Edit Procedural Sky",
        [&](Environment& env) {
            bool changed = false;

            const char* proceduralTooltip =
                "Bakes a Rayleigh+Mie atmosphere instead of the HDR, and drives the scene's key light: "
                "its direction, colour and intensity";
            changed |= propCheckbox("Enabled", &env.sky.procedural, proceduralTooltip);

            ImGui::BeginDisabled(!env.sky.procedural);

            changed |= propSlider(
                "Sun Elevation",
                &env.sky.sunElevation,
                -90.0f,
                90.0f,
                "%.0f deg",
                "Degrees above the horizon. Below zero is night; the key light follows this"
            );
            changed |= propSlider(
                "Sun Azimuth",
                &env.sky.sunAzimuth,
                -180.0f,
                180.0f,
                "%.0f deg",
                "Degrees around the horizon"
            );

            // The sky owns the key light while on, so its daylight look is authored here.
            const char* sunLightTooltip =
                "Key light colour with the sun overhead; lower down the atmosphere dims and reddens it. "
                "The sky drives the light, not the other way round";
            changed |= propColor3(
                "Sun Light",
                glm::value_ptr(env.sky.lightColor),
                ImGuiColorEditFlags_Float,
                sunLightTooltip
            );
            const char* sunIntensityTooltip =
                "The sun's illuminance with it overhead. It lights the sky too, so the sky's "
                "brightness follows it";
            changed |= propSlider(
                "Sun Light Intensity",
                &env.sky.lightIntensity,
                0.0f,
                20.0f,
                "%.2f",
                sunIntensityTooltip
            );
            ImGui::Separator();

            changed |= propSlider("Rayleigh", &env.sky.rayleigh, 0.0f, 4.0f, "%.2f");
            changed |= propSlider("Mie", &env.sky.mie, 0.0f, 10.0f, "%.2f");
            changed |= propSlider("Mie Asymmetry", &env.sky.mieG, 0.0f, SkySettings::MAX_MIE_G, "%.2f");
            const char* sunDiscTooltip =
                "The drawn sun's brightness per unit of the sun light's intensity; its size "
                "is the sun light's Source Size";
            changed |= propSlider(
                "Sun Disc Intensity",
                &env.sky.sunDiscIntensity,
                0.0f,
                20.0f,
                "%.1f",
                sunDiscTooltip
            );
            const char* aerialTooltip =
                "How much of the sky's air lies between the eye and the scene, as a scale on "
                "distance: far things fade into the horizon. 1 is the planet's, 0 none";
            changed |= propSlider(
                "Aerial Perspective",
                &env.sky.aerialPerspective,
                0.0f,
                4.0f,
                "%.2f",
                aerialTooltip
            );

            // Night takes over below the horizon on its own; only its look is set here.
            ImGui::Separator();
            const char* skyglowTooltip =
                "Lights the scene once the sun is down - the floor that keeps night dark "
                "rather than black";
            changed |= propColor3(
                "Night Skyglow",
                glm::value_ptr(env.night.radiance),
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR,
                skyglowTooltip
            );
            changed |= propSlider(
                "Star Intensity",
                &env.night.starIntensity,
                0.0f,
                10.0f,
                "%.1f",
                "0 removes the stars entirely"
            );
            changed |= propSlider(
                "Star Density",
                &env.night.starDensity,
                20.0f,
                400.0f,
                "%.0f",
                "Higher packs in more stars, each smaller"
            );
            changed |= propSlider(
                "Moon Tilt",
                &env.night.moonTilt,
                -60.0f,
                60.0f,
                "%.0f deg",
                "How far off the point exactly opposite the sun the moon sits"
            );
            changed |= propSlider("Moon Size", &env.night.moonAngularRadius, 0.002f, 0.2f, "%.3f");
            changed |= propSlider(
                "Moon Intensity",
                &env.night.moonIntensity,
                0.0f,
                10.0f,
                "%.1f",
                "How bright the moon looks; the halo around it follows"
            );
            changed |= propColor3(
                "Moonlight",
                glm::value_ptr(env.night.moonlightColor),
                ImGuiColorEditFlags_Float,
                "Key light colour after dark - the same light, aimed at the moon"
            );
            const char* moonlightIntensityTooltip =
                "How much the moon lights the world. Real moonlight is a tiny fraction of daylight";
            changed |= propSlider(
                "Moonlight Intensity",
                &env.night.moonlightIntensity,
                0.0f,
                2.0f,
                "%.2f",
                moonlightIntensityTooltip
            );
            ImGui::EndDisabled();
            return changed;
        }
    );

    editWorldCard(
        state,
        "Volumetric Fog",
        EditorStyle::Accent::ENV,
        scene.environment(),
        "Edit Volumetric Fog",
        [&](Environment& env) {
            bool changed = false;

            changed |= propCheckbox(
                "Enabled",
                &env.fog.enabled,
                "Froxel fog: scatters the scene lights (incl. local lights) through a height-falloff medium"
            );

            ImGui::BeginDisabled(!env.fog.enabled);
            changed |= propSlider("Density", &env.fog.density, 0.0f, 0.3f, "%.3f");
            changed |= propDrag("Height", &env.fog.height, 0.2f, -100.0f, 1000.0f, "%.1f");
            changed |= propSlider("Height Falloff", &env.fog.heightFalloff, 0.0f, 1.0f, "%.3f");
            changed |= propSlider(
                "Anisotropy",
                &env.fog.anisotropy,
                -FogSettings::MAX_ANISOTROPY,
                FogSettings::MAX_ANISOTROPY,
                "%.2f"
            );
            changed |= propColor3("Albedo", glm::value_ptr(env.fog.albedo));

            // Raise when point-light shafts look blocky; cost scales with X*Y*Z.
            changed |= propDragU32(
                "Froxels X",
                &env.fog.resolutionX,
                1.0f,
                FogSettings::MIN_FROXELS,
                FogSettings::MAX_FROXELS,
                "Screen-horizontal froxels. More = sharper light shafts."
            );
            changed |= propDragU32(
                "Froxels Y",
                &env.fog.resolutionY,
                1.0f,
                FogSettings::MIN_FROXELS,
                FogSettings::MAX_FROXELS
            );
            changed |= propDragU32(
                "Froxels Z",
                &env.fog.resolutionZ,
                1.0f,
                FogSettings::MIN_FROXELS,
                FogSettings::MAX_FROXELS,
                "Depth slices. More = smoother fog falloff with distance."
            );
            // The product is bounded too; say when that scales the grid down.
            const glm::uvec3 grid = env.fog.froxelGrid();
            if (grid != glm::uvec3(env.fog.resolutionX, env.fog.resolutionY, env.fog.resolutionZ)) {
                ImGui::TextDisabled(
                    "Allocated at %u x %u x %u: the grid holds at most %u froxels",
                    grid.x,
                    grid.y,
                    grid.z,
                    FogSettings::MAX_FROXEL_COUNT
                );
            }
            const char* maxDistanceTooltip =
                "How far from the eye the slices reach. Past it nothing more is scattered; "
                "nearer spends the slices where the fog is seen";
            changed |= propDrag(
                "Max Distance",
                &env.fog.maxDistance,
                1.0f,
                10.0f,
                2000.0f,
                "%.0f m",
                maxDistanceTooltip
            );
            ImGui::EndDisabled();
            return changed;
        }
    );

    editWorldCard(
        state,
        "Physics",
        EditorStyle::Accent::PHYSICS,
        scene.physics(),
        "Edit Physics",
        [&](PhysicsSettings& phys) {
            bool changed = propDrag3("Gravity", glm::value_ptr(phys.gravity), 0.05f, -50.0f, 50.0f, "%.2f");
            changed |= propDragInt("Solver Iterations", &phys.solverIterations, 0.1f, 1, 32);
            return changed;
        }
    );
}

void InspectorPanel::drawReflectionProbeSection(EditorContext& ec, EntityId id) {
    editComponentCard<ReflectionProbe>(ec, id, [&](ReflectionProbe& probe) {
        bool changed = false;

        // Influence and parallax box; should roughly match the region's walls.
        changed |= propDrag3("Box Size", glm::value_ptr(probe.halfExtents), 0.1f, 0.1f, 1000.0f, "%.1f");
        changed |= propSlider("Falloff", &probe.falloff, 0.0f, 1.0f, "%.2f");
        changed |= propDrag("Intensity", &probe.intensity, 0.02f, 0.0f, 8.0f, "%.2f");

        static const char* const RES_LABELS[] = {"128", "256", "512", "1024"};
        static const uint32_t    RES_VALUES[] = {128u, 256u, 512u, 1024u};
        const char* resolutionTooltip =
            "Cube face size for the bake. Shared across probes: the "
            "largest wins. Changing it re-bakes every probe.";
        changed |= propValueCombo("Resolution", RES_LABELS, RES_VALUES, &probe.resolution, resolutionTooltip);

        ImGui::Spacing();
        // Forces a bake after a scene change; automatic re-bakes are GLProbeManager's.
        changed |= rebakeButton(probe.bakeVersion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Captures the scene from the entity's Transform position");

        return changed;
    });
}

void InspectorPanel::drawDecalSection(EditorContext& ec, EntityId id) {
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<Decal>(ec, id, [&](Decal& decal) {
        bool changed = false;

        changed |= propCheckbox(
            "Enabled",
            &decal.enabled,
            "Off takes the projector out of the pass entirely"
        );

        // Its albedo, with alpha, is what lands on the surface.
        changed |= pickAsset<MaterialAsset>("##DecalMatPick", "Material", resources, decal.material);
        const char* angleFadeTooltip =
            "Fade where the surface turns away from the projector "
            "(projects along -Z; the Transform's scale is the box)";
        changed |= propSlider("Angle Fade", &decal.angleFade, 0.0f, 1.0f, "%.2f", angleFadeTooltip);
        changed |= propSlider("Opacity", &decal.opacity, 0.0f, 1.0f, "%.2f");

        // GLDecalPass skips a decal with no material outright.
        if (!decal.material) {
            ImGui::TextColored(EditorStyle::DANGER, "No material: this projects nothing");
        }

        return changed;
    });
}

void InspectorPanel::drawParticleSection(EditorContext& ec, EntityId id) {
    editComponentCard<ParticleEmitter>(ec, id, [&](ParticleEmitter& e) {
        bool changed = false;

        changed |= propCheckbox("Emitting", &e.emitting);
        changed |= propDrag("Rate", &e.rate, 0.5f, 0.0f, 2000.0f, "%.1f");
        changed |= propDrag("Lifetime", &e.lifetime, 0.05f, 0.01f, 60.0f, "%.2f");

        changed |= propDragU32("Max Particles", &e.maxParticles, 1.0f, 1u, 20000u);

        ImGui::Spacing();
        changed |= propDrag3("Velocity", glm::value_ptr(e.velocity), 0.05f, -100.0f, 100.0f, "%.2f");
        changed |= propDrag("Spread", &e.spread, 0.02f, 0.0f, 50.0f, "%.2f");
        changed |= propDrag3("Acceleration", glm::value_ptr(e.acceleration), 0.05f, -100.0f, 100.0f, "%.2f");

        ImGui::Spacing();
        changed |= propColor4("Start Color", glm::value_ptr(e.startColor));
        changed |= propColor4("End Color", glm::value_ptr(e.endColor));
        changed |= propDrag("Start Size", &e.startSize, 0.005f, 0.0f, 20.0f, "%.3f");
        changed |= propDrag("End Size", &e.endSize, 0.005f, 0.0f, 20.0f, "%.3f");
        changed |= propSlider(
            "Softness",
            &e.softness,
            0.0f,
            1.0f,
            "%.2f",
            "Edge falloff: 1 = soft blob, 0 = hard-edged crisp disc"
        );
        changed |= propCheckbox(
            "Additive",
            &e.additive,
            "Additive blend suits sparks/fire; alpha blend suits smoke"
        );

        const std::vector<Particle>* live = ec.frame.particles ? ec.frame.particles->of(id) : nullptr;
        ImGui::TextDisabled("Live: %d particle(s).", live ? static_cast<int>(live->size()) : 0);
        // Particles run off the sim delta, so Live is 0 in Edit mode.
        if (ec.frame.clock.getSimDelta() <= 0.0f) {
            ImGui::TextDisabled("The world is not running - press Play to see them.");
        }

        return changed;
    });
}

void InspectorPanel::drawAudioSourceSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<AudioSource>(ec, id, [&](AudioSource& source) {
        bool changed = false;

        changed |= pickAsset<AudioClipAsset>("##SoundPick", "Clip", resources, source.clip);

        const AudioClipAsset* clip = resources.tryGet(source.clip);
        if (clip) {
            ImGui::TextDisabled(
                "%.2fs, %u channel%s, %u Hz, %.1f MB",
                static_cast<double>(clip->duration()),
                clip->channels,
                clip->channels == 1 ? "" : "s",
                clip->sampleRate,
                static_cast<double>(clip->sampleCount() * sizeof(int16_t)) / (1024.0 * 1024.0)
            );
        }

        ImGui::Spacing();
        changed |= propSlider("Volume", &source.volume, 0.0f, 2.0f, "%.2f");
        changed |= propDrag(
            "Pitch",
            &source.pitch,
            0.005f,
            0.1f,
            4.0f,
            "%.2fx",
            "Playback rate; also shifts the pitch"
        );
        changed |= propCheckbox("Loop", &source.loop);
        const char* playOnStartTooltip =
            "Starts by itself once the simulation runs. In the editor that "
            "means on Play, never while a scene is only open";
        changed |= propCheckbox("Play On Start", &source.playOnStart, playOnStartTooltip);

        ImGui::Spacing();
        const char* spatialTooltip =
            "Positioned in the world and attenuated by distance. Turn it off "
            "for music, narration and UI sound";
        changed |= propCheckbox("Spatial", &source.spatial, spatialTooltip);
        if (source.spatial) {
            changed |= propDrag(
                "Min Distance",
                &source.minDistance,
                0.05f,
                0.0f,
                1000.0f,
                "%.2f",
                "Full volume inside this radius"
            );
            changed |= propDrag(
                "Max Distance",
                &source.maxDistance,
                0.25f,
                0.0f,
                5000.0f,
                "%.1f",
                "Silent at this radius; the falloff between the two is linear"
            );
            if (source.maxDistance <= source.minDistance) {
                ImGui::TextColored(
                    EditorStyle::WARNING,
                    "Max Distance is not past Min - nothing is attenuated."
                );
            }
            if (clip && clip->channels > 1) {
                ImGui::TextColored(
                    EditorStyle::WARNING,
                    "This clip has %u channels - each sticks to one ear.",
                    clip->channels
                );
            }
            // Without one it plays at the world origin.
            if (!scene.has<Transform>(id)) {
                ImGui::TextColored(
                    EditorStyle::WARNING,
                    "A positioned sound needs a Transform to have a position."
                );
            }
            // AudioSystem::warnIfNoListener waits for a spatial voice, so at edit time only this says it.
            if (!findActiveListener(scene)) {
                ImGui::TextColored(
                    EditorStyle::WARNING,
                    "No active Audio Listener in the scene - this is silent."
                );
            }
        }

        // Not spatial-only: a muted mix silences everything, auditions too, though the cursor runs.
        if (ec.audioSystem.device().masterVolume() <= 0.0f) {
            ImGui::TextColored(
                EditorStyle::WARNING,
                "Audio Listener volume is 0 - nothing is heard, audition included."
            );
        }

        ImGui::Spacing();
        // The Asset Browser's transport; see auditionTransport.
        AudioDevice& device = ec.audioSystem.device();
        const float  ih     = ImGui::GetFrameHeight();
        if (auditionTransport("inspSound", device, m_previewVoice, m_previewOwner == id, clip, ih))
            m_previewOwner = id;

        ImGui::SameLine(0, EditorStyle::px(8.0f));
        ImGui::AlignTextToFramePadding();
        // From the mixer: `playing` is the scene's word, not the sound's.
        const VoiceId sourceVoice = ec.audioSystem.voiceOf(id);
        if (device.isVoiceActive(sourceVoice)) {
            ImGui::TextDisabled(
                "Source: %s %.2fs",
                device.isVoicePaused(sourceVoice) ? "held" : "playing",
                static_cast<double>(device.voiceCursor(sourceVoice))
            );
        } else {
            ImGui::TextDisabled("%s", source.playing ? "Source: playing" : "Source: idle");
        }

        // Disabled, not hidden, so the card keeps its height when an audition ends.
        auditionScrubber("SndTime", device, m_previewVoice, clip != nullptr ? clip->duration() : 0.0f, -1.0f);

        return changed;
    });
}

void InspectorPanel::drawAudioListenerSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<AudioListener>(ec, id, [&](AudioListener& listener) {
        bool changed = false;

        const char* activeTooltip =
            "Exactly one listener is heard from; of several active ones, "
            "the lowest-numbered entity wins";
        changed |= propCheckbox("Active", &listener.active, activeTooltip);
        changed |= propSlider(
            "Volume",
            &listener.volume,
            0.0f,
            1.0f,
            "%.2f",
            "Master gain for everything this listener hears"
        );

        // Names the winner. Needs this listener's Transform: findActiveListener joins on it, so one
        // without lost for the reason the warning below gives.
        const EntityId heard = findActiveListener(scene);
        if (listener.active && heard && heard != id && scene.has<Transform>(id)) {
            char winner[64] = {};
            getEntityDisplayName(scene, heard, winner, sizeof(winner));
            // Wrapped: it carries a user-typed name that could overflow the panel.
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped(
                "Not the ear: '%s' (#%u) is heard from - of two active "
                "listeners, the lower-numbered entity wins.",
                winner,
                heard.slot()
            );
            ImGui::PopStyleColor();
        }
        if (listener.active && !scene.has<Transform>(id)) {
            ImGui::TextColored(EditorStyle::WARNING, "A listener needs a Transform to have a position.");
        }

        return changed;
    });
}

void InspectorPanel::drawIrradianceVolumeSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<IrradianceVolume>(ec, id, [&](IrradianceVolume& v) {
        bool changed = false;

        changed |= propDrag3("Box Size", glm::value_ptr(v.halfExtents), 0.1f, 0.1f, 1000.0f, "%.1f");

        constexpr uint32_t MAX_PROBES = IrradianceVolume::MAX_RESOLUTION;
        changed |= propDragU32("Probes X", &v.resolutionX, 0.1f, 1u, MAX_PROBES);
        changed |= propDragU32("Probes Y", &v.resolutionY, 0.1f, 1u, MAX_PROBES);
        changed |= propDragU32("Probes Z", &v.resolutionZ, 0.1f, 1u, MAX_PROBES);

        changed |= propDrag("Intensity", &v.intensity, 0.02f, 0.0f, 8.0f, "%.2f");
        changed |= propDrag(
            "Blend Distance",
            &v.blendDistance,
            0.05f,
            0.0f,
            50.0f,
            "%.2f m",
            "How far inside the box its light fades in from the sky's; 0 is a hard edge"
        );

        ImGui::Spacing();
        changed |= rebakeButton(v.bakeVersion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Every probe is twelve scene captures, and the bake runs\n"
                "inside one frame - the editor stops until it finishes.\n"
                "The grid is a product, so this is cubic in the sliders."
            );
        // Twelve captures a probe: GLIrradianceBaker takes a radiance and a backface cube. The
        // clamped grid is what is baked and allocated; the raw fields are the file's.
        const glm::uvec3 grid = IrradianceVolume::clampedResolution(v);
        const uint32_t probeCount = grid.x * grid.y * grid.z;
        ImGui::TextDisabled("%u probes, %u scene captures.", probeCount, probeCount * 12u);

        // A scene lights from one volume; a second bakes nothing and looks merely out of range.
        // findIrradianceVolume names the one the frame reads.
        const EntityId lit = findIrradianceVolume(scene);
        if (lit && lit != id) {
            char name[64] = {};
            getEntityDisplayName(scene, lit, name, sizeof(name));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped("Not the scene's volume: '%s' is baked.", name);
            ImGui::PopStyleColor();
        }

        return changed;
    });
}

void InspectorPanel::drawRigidbodySection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<Rigidbody>(ec, id, [&](Rigidbody& rb) {
        bool changed = false;

        changed |= propEnumCombo("Motion", rb.motion);
        changed |= propDrag("Mass", &rb.mass, 0.1f, 0.01f, 1000.0f, "%.2f");
        changed |= propDrag("Gravity Scale", &rb.gravityScale, 0.05f, 0.0f, 10.0f, "%.2f");
        changed |= propSlider("Restitution", &rb.restitution, 0.0f, 1.0f, "%.2f");
        changed |= propSlider("Friction", &rb.friction, 0.0f, 2.0f, "%.2f");
        changed |= propSlider("Linear Damping", &rb.linearDamping, 0.0f, 1.0f, "%.3f");
        changed |= propSlider("Angular Damping", &rb.angularDamping, 0.0f, 1.0f, "%.3f");

        changed |= drawVec3Control("Velocity", glm::value_ptr(rb.linearVelocity), 0.0f, 0.1f);
        changed |= drawVec3Control("Angular Vel", glm::value_ptr(rb.angularVelocity), 0.0f, 0.1f);
        changed |= propCheckbox(
            "Freeze Rotation",
            &rb.freezeRotation,
            "Translation only: contacts never torque the body (character controllers)"
        );
        changed |= propCheckbox(
            "Can Sleep",
            &rb.canSleep,
            "Uncheck for script-driven bodies that must stay responsive at rest"
        );

        changed |= propDragInt(
            "Layer",
            &rb.layer,
            0.1f,
            0,
            1 << 30,
            "Bit mask of the layers this body is on; a ragdoll puts its bones on 2"
        );
        changed |= propDragInt(
            "Collides With",
            &rb.collidesWith,
            0.1f,
            INT_MIN,
            INT_MAX,
            "Bit mask of layers this body collides with; -1 is everything"
        );

        // Only a dynamic body falls out of the world without a shape; others are merely inert.
        if (rb.motion == RigidbodyMotion::Dynamic && !scene.has<Collider>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Collider: it falls through everything.");
        }

        if (changed) {
            // PhysicsSystem zeroes a sleeping body's velocity: wake it on the live component, so the
            // wake is in the undo command's "after" value.
            Rigidbody::wake(rb);
        }
        return changed;
    });
}

void InspectorPanel::drawColliderSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<Collider>(ec, id, [&](Collider& col) {
        bool changed = false;

        changed |= propCheckbox(
            "Enabled",
            &col.enabled,
            "Disabled colliders are inert: no broadphase entry, no contacts"
        );

        if (col.parts.size() == 1) {
            ColliderPart& part = col.parts[0];
            changed |= propEnumCombo("Shape", part.shape);
            changed |= drawVec3Control("Center", glm::value_ptr(part.center), 0.0f, 0.05f);
            switch (part.shape) {
                case ColliderShape::Capsule:
                    // Along local +Y; total height 2*(halfHeight + radius).
                    changed |= propDrag("Radius", &part.radius, 0.01f, 0.001f, 1000.0f, "%.3f");
                    changed |= propDrag(
                        "Half Height",
                        &part.halfHeight,
                        0.01f,
                        0.0f,
                        1000.0f,
                        "%.3f",
                        "Half the segment, caps excluded. 0 is a sphere."
                    );
                    ImGui::TextDisabled("Height %.3f along local +Y", (part.halfHeight + part.radius) * 2.0f);
                    break;

                case ColliderShape::Mesh:
                    changed |= pickAsset<MeshAsset>("##ColliderMeshPick", "Mesh", resources, part.mesh);
                    changed |= drawVec3Control(
                        "Mesh Scale",
                        glm::value_ptr(part.meshScale),
                        1.0f,
                        0.05f,
                        0.001f,
                        1000.0f
                    );
                    if (col.meshPoints.empty()) {
                        ImGui::TextColored(EditorStyle::DANGER, "No triangles: this collides with nothing.");
                        ImGui::TextDisabled("Pick a mesh, or Make Mesh Collider below.");
                        break;
                    }
                    ImGui::TextDisabled(
                        "%zu triangle(s), %zu tree node(s)",
                        col.meshPoints.size() / 3,
                        col.meshNodes.size()
                    );
                    // For level geometry: two meshes never collide, so a moving one silently drops through.
                    if (const Rigidbody* rb = scene.tryGet<Rigidbody>(id);
                        rb && rb->motion == RigidbodyMotion::Dynamic) {
                        ImGui::TextColored(
                            EditorStyle::DANGER,
                            "Meshes never collide with each other: this body falls through mesh floors."
                        );
                    }
                    break;

                case ColliderShape::Box:
                    changed |= drawVec3Control(
                        "Half Extents",
                        glm::value_ptr(part.halfExtents),
                        0.5f,
                        0.05f,
                        0.001f,
                        1000.0f
                    );
                    break;

                case ColliderShape::Count:
                    break;
            }
        } else {
            ImGui::TextDisabled("%zu parts (mesh-fitted)", col.parts.size());
        }

        // Detail 1: scaled bounds; higher: a box compound, entity scale baked in (the solver ignores
        // it). Searched downward: an import puts the geometry below the physics entity.
        const EntityId meshNode = HierarchyOperations::findInSelfOrDescendants<Mesh>(scene, id);
        const Mesh* source = scene.tryGet<Mesh>(meshNode);
        if (source && source->mesh) {
            const auto& asset = resources.get(source->mesh);
            if (const Name* from = meshNode != id ? scene.tryGet<Name>(meshNode) : nullptr) {
                ImGui::Spacing();
                ImGui::TextDisabled("Shape from '%s'", from->value);
            }
            if (asset.bounds().valid()) {
                ImGui::Spacing();
                propSliderInt(
                    "Detail",
                    &m_colliderFitDetail,
                    1,
                    COLLIDER_FIT_MAX_DETAIL,
                    "1 = one box; higher = a tighter box compound (more boxes = heavier)"
                );
                if (ImGui::Button("Fit to Mesh", ImVec2(-1.0f, 0.0f))) {
                    const glm::vec3 scale = meshScaleRelativeTo(scene, id, meshNode);
                    col.parts = fitBoxesToMesh(asset, m_colliderFitDetail, scale);
                    changed = true;
                }

                // The mesh's own geometry, for static things; replaces the parts, or both would collide.
                if (ImGui::Button("Make Mesh Collider", ImVec2(-1.0f, 0.0f))) {
                    // Swapped in only if non-empty: a mesh with no whole triangle would leave no collider.
                    const glm::vec3 scale = meshScaleRelativeTo(scene, id, meshNode);
                    Collider built;
                    built.parts.clear();
                    const bool empty = addMeshCollider(built, source->mesh, resources, scale) == 0;
                    m_meshColliderEmpty = empty ? id : EntityId{};
                    if (!empty) {
                        built.isTrigger = col.isTrigger;
                        built.enabled   = col.enabled;
                        col = std::move(built);
                        changed = true;
                    }
                }
                if (m_meshColliderEmpty == id) {
                    ImGui::TextColored(
                        EditorStyle::DANGER,
                        "That mesh has no whole triangle; the collider is unchanged."
                    );
                }
            }
        }

        changed |= propCheckbox("Trigger", &col.isTrigger);

        // PhysicsSystem reads Colliders only via Rigidbodies: alone it neither blocks nor triggers.
        if (!scene.has<Rigidbody>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Rigidbody: nothing collides with this.");
        }

        // Inside the edit, so undo restores the triangles and the viewport shows them at once.
        if (changed) syncMeshCollider(col, resources);
        return changed;
    });
}

void InspectorPanel::drawJointSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<Joint>(ec, id, [&](Joint& joint) {
        bool changed = false;

        changed |= propEnumCombo("Type", joint.type);

        // Every entity, named or not; the scene is walked only while the list is open.
        char preview[96] = "None";
        if (scene.isAlive(joint.connected)) {
            getEntityDisplayName(scene, joint.connected, preview, sizeof(preview));
        }
        // In an instance it is the prefab's: an override cannot name an entity, so a pick would not load.
        const bool prefabOwned = static_cast<bool>(PrefabOverrides::instanceRoot(scene, id));
        ImGui::BeginDisabled(prefabOwned);
        changed |= propRow("Connected", nullptr, [&] {
            if (!beginCombo("##v", preview)) return false;
            const EntityId was = joint.connected;
            if (ImGui::Selectable("None", !scene.isAlive(was))) joint.connected = {};
            scene.forEachEntity([&](EntityId other) {
                if (other == id) return;
                char label[96];
                getEntityDisplayName(scene, other, label, sizeof(label));
                ImGui::PushID(static_cast<int>(other.slot()));
                if (ImGui::Selectable(label, other == was)) joint.connected = other;
                ImGui::PopID();
            });
            ImGui::EndCombo();
            return joint.connected != was;
        });
        ImGui::EndDisabled();
        if (prefabOwned) ImGui::TextDisabled("Set in the prefab: an override cannot name an entity.");

        changed |= drawVec3Control("Anchor", glm::value_ptr(joint.anchor), 0.0f, 0.05f);
        changed |= drawVec3Control("Connected Anchor", glm::value_ptr(joint.connectedAnchor), 0.0f, 0.05f);

        if (joint.type == JointType::Distance) {
            const char* distanceTooltip =
                "Negative takes whatever the two were apart on the\n"
                "first tick, so a rope built at play time needs no\n"
                "one to measure it.";
            changed |= propDrag(
                "Distance",
                &joint.distance,
                0.01f,
                -1.0f,
                1000.0f,
                "%.3f m",
                distanceTooltip
            );
            // The solver's measurement: shown, not edited.
            if (joint.distance < 0.0f && joint.resolvedDistance >= 0.0f) {
                ImGui::TextDisabled("Holding %.3f m, measured on the first tick.", joint.resolvedDistance);
            }
        }

        const char* stiffnessTooltip =
            "Fraction of the remaining gap closed per tick.\n"
            "1 pulls the anchors together at once; lower drifts back slowly.";
        changed |= propSlider("Stiffness", &joint.stiffness, 0.0f, 1.0f, "%.2f", stiffnessTooltip);
        const char* holdTorqueTooltip =
            "Most torque spent keeping the two at the angle\n"
            "they had when the joint began to move them.\n"
            "A heavier load turns the joint; 0 leaves it free.";
        changed |= propDrag(
            "Hold Torque",
            &joint.holdTorque,
            1.0f,
            0.0f,
            10000.0f,
            "%.1f N m",
            holdTorqueTooltip
        );
        const char* collideConnectedTooltip =
            "Off by default: jointed bodies usually overlap at\n"
            "the joint, and resolving both the contact and the\n"
            "joint makes the pair fight and gain energy.";
        changed |= propCheckbox("Collide Connected", &joint.collideConnected, collideConnectedTooltip);

        if (!joint.connected) {
            ImGui::TextColored(EditorStyle::DANGER, "No connected body: this holds nothing.");
        }
        if (!scene.has<Rigidbody>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Rigidbody: this entity is not simulated.");
        }

        return changed;
    });
}

void InspectorPanel::drawRagdollSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;
    EditorState& state = ec.state;

    // Run after the card: it holds a reference to the component, which both operations remove.
    enum class Pending { None, Build, Clear };
    Pending pending = Pending::None;
    EntityId rigNode{};

    editComponentCard<Ragdoll>(ec, id, [&](Ragdoll& ragdoll) {
        bool changed = false;

        const char* activeTooltip =
            "On, physics poses the rig and the clip is ignored.\n"
            "Off, the bodies follow the animation and do not fall.";
        changed |= propCheckbox("Active", &ragdoll.active, activeTooltip);

        ImGui::TextDisabled("%zu simulated bone(s)", ragdoll.bones.size());

        // Before the rig lookup: clearing needs none, the bones being ordinary entities.
        if (ImGui::Button("Clear", ImVec2(-1.0f, 0.0f))) pending = Pending::Clear;

        // Searched downward: an import puts the Animator below the physics entity.
        rigNode = HierarchyOperations::findInSelfOrDescendants<Animator>(scene, id);
        const Animator* rig = scene.tryGet<Animator>(rigNode);
        if (!rig || !resources.tryGet(rig->skeleton)) {
            ImGui::TextColored(
                EditorStyle::DANGER,
                "No Animator with a skeleton here or below: nothing to build from."
            );
            return changed;
        }
        if (rigNode != id) {
            if (const Name* named = scene.tryGet<Name>(rigNode)) {
                ImGui::TextDisabled("Rig from '%s'", named->value);
            }
        }

        ImGui::Spacing();
        const char* thicknessTooltip =
            "Limb radius as a fraction of its length. A rig says nothing\n"
            "about how solid it is, and length is what scales.";
        propDrag("Thickness", &m_ragdollSettings.thickness, 0.01f, 0.02f, 1.0f, "%.2f", thicknessTooltip);
        propDrag(
            "Mass",
            &m_ragdollSettings.mass,
            1.0f,
            0.1f,
            1000.0f,
            "%.0f kg",
            "Shared out by limb volume, so a forearm does not weigh a torso."
        );
        const char* muscleTooltip =
            "How much of its shape the body keeps once limp.\n"
            "0 folds where it stands; 1 holds every limb out\n"
            "against its own weight. Sets each joint's Hold Torque.";
        propSlider("Muscle", &m_ragdollSettings.muscle, 0.0f, 1.0f, "%.2f", muscleTooltip);

        if (ImGui::Button("Build", ImVec2(-1.0f, 0.0f))) pending = Pending::Build;

        ImGui::TextDisabled("Clear removes the ragdoll and its bones together.");

        return changed;
    });

    // Both rebuild the subtree, so both record a before/after step (design.md 2.5).
    if ((pending == Pending::Build && rigNode) || pending == Pending::Clear) {
        SubtreeSnapshot before = SubtreeSnapshot::capture(scene, id);

        if (pending == Pending::Build) {
            const SkeletonAsset& rig = resources.get(scene.get<Animator>(rigNode).skeleton);
            buildRagdoll(scene, id, rig, m_ragdollSettings);
        } else {
            clearRagdoll(scene, id);
        }

        SubtreeSnapshot after = SubtreeSnapshot::capture(scene, id);
        auto replace = std::make_unique<SubtreeReplaceCommand>(
            std::move(before),
            std::move(after),
            pending == Pending::Build ? "Build ragdoll" : "Clear ragdoll"
        );
        state.pushStep(std::move(replace));
    }
}

void InspectorPanel::drawCameraSection(EditorContext& ec, EntityId id) {
    Scene&       scene = ec.frame.scene;
    EditorState& state = ec.state;

    editComponentCard<Camera>(ec, id, [&](Camera& cam) {
        bool changed = false;

        changed |= propEnumCombo("Projection", cam.projection);

        if (cam.projection == ProjectionType::Perspective) {
            changed |= propAngleSlider("FOV", &cam.fovY, 10.0f, 170.0f);
        } else {
            changed |= propDrag("Ortho Height", &cam.orthoHeight, 0.1f, 0.1f, 1000.0f);
        }

        // <= 0 tracks the viewport (default); a value pins the ratio.
        bool autoAspect = cam.aspect <= 0.0f;
        const char* autoAspectTooltip = "Derive the aspect ratio from the viewport each frame";
        if (propCheckbox("Auto Aspect", &autoAspect, autoAspectTooltip)) {
            cam.aspect = autoAspect ? 0.0f : 16.0f / 9.0f;
            changed = true;
        }
        if (!autoAspect)
            changed |= propDrag("Aspect", &cam.aspect, 0.01f, 0.1f, 10.0f, "%.3f");

        changed |= propDrag("Near Clip", &cam.zNear, 0.01f, 0.001f, cam.zFar - CLIP_PLANE_SEPARATION, "%.3f");
        changed |= propDrag(
            "Far Clip",
            &cam.zFar,
            1.0f,
            cam.zNear + CLIP_PLANE_SEPARATION,
            100000.0f,
            "%.0f"
        );

        // Amount 0 disables the DoF pass.
        changed |= propDrag("Focus Distance", &cam.focusDistance, 0.1f, 0.01f, 10000.0f, "%.2f");
        changed |= propSlider("DoF Amount", &cam.dofAmount, 0.0f, 1.0f, "%.2f");
        const char* dofMaxBlurTooltip =
            "Widest blur radius, as a fraction of the viewport's height, so the "
            "look holds at any resolution (0.011 is about 12 px at 1080p)";
        changed |= propSlider("DoF Max Blur", &cam.dofMaxBlur, 0.0f, 0.05f, "%.3f", dofMaxBlurTooltip);
        changed |= propCheckbox("Active", &cam.active);

        // The game's eye: the frame's camera while the game has the viewport, else the one a
        // session would start on.
        const EntityId eye = ec.input.gameHasViewport() && ec.frame.visibility
            ? ec.frame.visibility->cameraEntity
            : findActiveCamera(scene);
        if (cam.active && eye && eye != id && scene.has<Camera>(eye)) {
            char rendered[64] = {};
            getEntityDisplayName(scene, eye, rendered, sizeof(rendered));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::WARNING);
            ImGui::TextWrapped("Not the game's eye: it renders from '%s'.", rendered);
            ImGui::PopStyleColor();
        }

        if (ImGui::Button("Set as Main Camera", ImVec2(-1, 0))) {
            pushActiveCamera(scene, ec.frame.resources, state, id);
        }

        return changed;
    });
}

void InspectorPanel::drawLODSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<LOD>(ec, id, [&](LOD& lod) {
        bool changed = false;

        changed |= propSlider(
            "Bias",
            &lod.bias,
            0.1f,
            4.0f,
            "%.2f",
            "Scales every level's range; above 1 keeps detail further out"
        );

        for (size_t i = 0; i < lod.levels.size(); ++i) {
            const LODLevel&  level = lod.levels[i];
            const MeshAsset* mesh  = resources.tryGet(level.mesh);
            const char*      name  = mesh ? mesh->name().c_str() : "<unresolved>";
            const size_t     tris  = mesh ? mesh->indices.size() / 3 : 0;
            ImGui::TextDisabled("%zu: %s  (%zu tris, to %.0fm)", i, name, tris, level.maxDistance);
        }

        ImGui::Spacing();

        const Mesh* source = scene.tryGet<Mesh>(id);
        if (source && source->mesh) {
            propSliderInt(
                "Levels",
                &m_lodGenLevels,
                1,
                4,
                "How many coarser levels to build below the source mesh"
            );
            if (ImGui::Button("Generate Levels", ImVec2(-1.0f, 0.0f))) {
                lod = generateLOD(resources, source->mesh, static_cast<uint32_t>(m_lodGenLevels));
                changed = true;
            }
        } else {
            ImGui::TextDisabled("Add a Mesh to generate levels from.");
        }

        return changed;
    });
}

void InspectorPanel::drawAnimationSection(EditorContext& ec, EntityId id) {
    Scene&           scene     = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    // The world is not stepping, so the playhead shows by posing. Outside play that is the saved
    // Transform, recorded as AnimationPanel::drawEditor does; in play it is session state, and
    // a step would drop the history Stop keeps.
    Transform* transform = scene.tryGet<Transform>(id);
    const Transform beforePose = transform ? *transform : Transform{};

    editComponentCard<Animation>(ec, id, [&](Animation& anim) {
        const auto pose = [&] { if (transform) AnimationSystem::applyAnimation(anim, *transform); };

        // Loop and Speed are saved, so they push an edit; Play, Stop and scrubbing only pose.
        const ClipTransport transport = clipTransport("anim", anim, 0.0f, -1.0f);
        if (transport.rewound) pose();
        bool changed = transport.authored;

        // Holds the clip open past the last keyframe; 0 derives it from the keyframes.
        if (propDrag("Length", &anim.length, 0.02f, 0.0f, 100000.0f, "%.2f s  (0 = auto)")) {
            anim.length = std::max(0.0f, anim.length);
            changed = true;
        }

        // The transport previews; this is what a shipped scene does.
        const char* playOnStartTooltip =
            "Starts by itself once the simulation runs. In the editor that "
            "means on Play, never while a scene is only open";
        changed |= propCheckbox("Play On Start", &anim.playOnStart, playOnStartTooltip);

        // Only AnimationSystem::fixedUpdate advances playback, so with the clock paused it holds.
        if (anim.playing && ec.frame.clock.getSimDelta() <= 0.0f) {
            ImGui::TextDisabled("Held at %.2fs - it advances while the world runs.", anim.time);
        }

        const float duration = Animation::computeDuration(anim);
        if (duration > 0.0f) {
            ImGui::SetNextItemWidth(-1);
            char timeFmt[32];
            snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", duration);
            if (sliderFloat("##ATime", &anim.time, 0.0f, duration, timeFmt)) {
                anim.playing = false;
                pose();
            }
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Keyframes");
        auto trackSummary = [](const char* label, size_t count, float dur) {
            ImGui::BulletText("%s: %zu key%s, %.2fs", label, count, count == 1 ? "" : "s", dur);
        };
        trackSummary("Position", anim.positionTrack.keyframeCount(), anim.positionTrack.getDuration());
        trackSummary("Rotation", anim.rotationTrack.keyframeCount(), anim.rotationTrack.getDuration());
        trackSummary("Scale", anim.scaleTrack.keyframeCount(), anim.scaleTrack.getDuration());
        ImGui::TextDisabled("Edit keyframes in the Animation panel.");

        return changed;
    });

    // Still valid: the card's remove takes the Animation, not the Transform.
    if (!transform || m_sessionPlaying) return;
    if (transform->position == beforePose.position && transform->rotation == beforePose.rotation
        && transform->scale == beforePose.scale) {
        return;
    }
    pushEdit<Transform>(scene, resources, ec.state, id, beforePose, *transform, "Scrub Animation");
}

void InspectorPanel::drawAnimatorSection(EditorContext& ec, EntityId id) {
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<Animator>(ec, id, [&](Animator& animator) {
        bool changed = false;

        changed |= pickAsset<SkeletonAsset>("##RigPick", "Rig", resources, animator.skeleton);
        changed |= pickAsset<AnimationClipAsset>("##ClipPick", "Clip", resources, animator.clip);

        const SkeletonAsset* rig = resources.tryGet(animator.skeleton);
        const AnimationClipAsset* clip = resources.tryGet(animator.clip);

        if (rig) ImGui::TextDisabled("%zu bones", rig->bones.size());
        // A clip is cooked against one rig's bone order; SkeletalAnimationSystem::resolveClip refuses
        // a mismatch and holds the bind pose. Said where the pairing is made.
        if (rig && clip && clip->skeleton != rig->name()) {
            ImGui::TextColored(EditorStyle::DANGER, "Clip belongs to rig '%s'", clip->skeleton.c_str());
            ImGui::TextDisabled("The bind pose is held until they match.");
        }

        // Loop and Speed push an edit; play, stop and scrubbing do not. Scrubbing works paused:
        // SkeletalAnimationSystem::update still poses the rig.
        changed |= clipTransport("rig", animator, -10.0f, -1.0f).authored;

        const char* playOnStartTooltip =
            "Starts by itself once the simulation runs. In the editor that "
            "means on Play, never while a scene is only open";
        changed |= propCheckbox("Play On Start", &animator.playOnStart, playOnStartTooltip);

        // Only SkeletalAnimationSystem::fixedUpdate moves the head, so in Edit mode it holds.
        if (animator.playing && ec.frame.clock.getSimDelta() <= 0.0f) {
            ImGui::TextDisabled(
                "Held at %.2fs - it advances while the world runs.",
                static_cast<double>(animator.time)
            );
        }

        if (clip && clip->duration > 0.0f) {
            ImGui::SetNextItemWidth(-1);
            char timeFmt[32];
            snprintf(timeFmt, sizeof(timeFmt), "%%.2f / %.2f s", clip->duration);
            sliderFloat("##RigTime", &animator.time, 0.0f, clip->duration, timeFmt);
        } else if (animator.skeleton) {
            ImGui::TextDisabled("No clip: holding the bind pose.");
        }

        if (clip && !clip->markers.empty()) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Markers");
            for (const ClipMarker& marker : clip->markers) {
                ImGui::BulletText("%s at %.2fs", marker.name.c_str(), static_cast<double>(marker.time));
            }
            ImGui::TextDisabled("Fired as AnimationEvent; edit them in the clip's recipe.");
        }

        // No blend state here or in the file: crossfades start from code (Animator::crossFadeTo).
        return changed;
    });
}

void InspectorPanel::drawBoneSocketSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    ResourceManager& resources = ec.frame.resources;

    editComponentCard<BoneSocket>(ec, id, [&](BoneSocket& socket) {
        bool changed = false;

        // The rig must be the parent: BoneSocketSystem writes the local Transform against it, so
        // one hung deeper lands plausibly wrong.
        const Hierarchy*     node     = scene.tryGet<Hierarchy>(id);
        const EntityId       rig      = node ? node->parent : EntityId{};
        const Animator*      animator = scene.tryGet<Animator>(rig);
        const SkeletonAsset* skeleton = animator ? resources.tryGet(animator->skeleton) : nullptr;

        if (!animator) {
            ImGui::TextColored(EditorStyle::DANGER, "The parent is not a rig");
            ImGui::TextDisabled(
                "A socket hangs off the entity carrying the Animator. "
                "Drag this entity onto it in the Hierarchy."
            );
        } else if (!skeleton) {
            ImGui::TextColored(EditorStyle::DANGER, "The rig above names no skeleton");
            ImGui::TextDisabled("Pick one on its Animator card.");
        }

        changed |= pickBone("##BonePick", skeleton, socket.bone);

        // An unknown bone name fails silently: the socket stays put.
        if (skeleton && !socket.bone.empty() && skeleton->indexOf(socket.bone) < 0) {
            ImGui::TextColored(
                EditorStyle::DANGER,
                "Rig '%s' has no bone '%s'",
                skeleton->name().c_str(),
                socket.bone.c_str()
            );
            ImGui::TextDisabled("The socket stays where it is until they match.");
        } else if (skeleton) {
            ImGui::TextDisabled("%zu bones in rig '%s'", skeleton->bones.size(), skeleton->name().c_str());
        }

        // Authored here: the Transform is the socket's output, rewritten every frame, paused too.
        ImGui::Spacing();
        sectionLabel("Offset");
        changed |= drawVec3Control("Position", glm::value_ptr(socket.offset.position), 0.0f, 0.01f);

        m_socketEulerCache.sync(id, socket.offset.rotation);
        if (drawVec3Control("Rotation", m_socketEulerCache.degrees(), 0.0f, 0.5f)) {
            socket.offset.rotation = m_socketEulerCache.toQuat();
            changed = true;
        }

        changed |= drawVec3Control("Scale", glm::value_ptr(socket.offset.scale), 1.0f, 0.01f);

        return changed;
    });
}

void InspectorPanel::drawCharacterControllerSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;

    editComponentCard<CharacterController>(ec, id, [&](CharacterController& cc) {
        bool changed = false;

        changed |= propDrag("Jump Speed", &cc.jumpSpeed, 0.1f, 0.0f, 100.0f, "%.2f m/s");
        changed |= propDrag(
            "Acceleration",
            &cc.acceleration,
            0.5f,
            0.0f,
            1000.0f,
            "%.1f m/s2",
            "How fast velocity closes on the requested direction"
        );
        changed |= propSlider(
            "Air Control",
            &cc.airControl,
            0.0f,
            1.0f,
            "%.2f",
            "Fraction of that acceleration available while airborne"
        );
        const char* maxSlopeTooltip =
            "Steeper than this holds nothing up: the character slides.\n"
            "The same angle decides what counts as a wall to run\n"
            "along rather than walk into, and how tall a step the\n"
            "capsule rolls over.";
        changed |= propDrag("Max Slope", &cc.maxSlopeAngle, 0.5f, 0.0f, 90.0f, "%.0f deg", maxSlopeTooltip);

        const char* stepHeightTooltip =
            "Tallest thing the character mounts instead of stopping at.\n"
            "Checked against real geometry before anything moves: there\n"
            "has to be clear space above it and walkable ground beyond,\n"
            "so raising this makes the character climb more, never\n"
            "climb through. Zero switches it off and a kerb is a wall.";
        changed |= propDrag("Step Height", &cc.stepHeight, 0.01f, 0.0f, 10.0f, "%.2f m", stepHeightTooltip);

        // A different number: an edge below this gives a walkable normal even without step-up.
        if (const float radius = capsuleRadiusOf(scene, id); radius > 0.0f) {
            const float limit = glm::radians(glm::clamp(cc.maxSlopeAngle, 0.0f, 90.0f));
            ImGui::Spacing();
            ImGui::TextDisabled("Rolls over steps up to %.2f m unaided", radius * (1.0f - std::cos(limit)));
        }

        // Live state: moveInput is gameplay's, the rest the system's.
        ImGui::Spacing();
        ImGui::TextDisabled("%s", cc.grounded ? "Grounded" : "Airborne");
        if (cc.grounded) {
            ImGui::TextDisabled(
                "Ground %.0f deg from flat",
                glm::degrees(std::acos(glm::clamp(cc.groundNormal.y, -1.0f, 1.0f)))
            );
        }
        ImGui::TextDisabled("Move input %.2f, %.2f, %.2f", cc.moveInput.x, cc.moveInput.y, cc.moveInput.z);

        const Rigidbody* body = scene.tryGet<Rigidbody>(id);
        if (!body) {
            ImGui::TextColored(EditorStyle::DANGER, "No Rigidbody: nothing to drive.");
        } else if (!body->freezeRotation) {
            ImGui::TextColored(EditorStyle::DANGER, "Rigidbody rotation is not frozen:");
            ImGui::TextDisabled("contacts will topple the character.");
        }
        if (!scene.has<Collider>(id)) {
            ImGui::TextColored(EditorStyle::DANGER, "No Collider: it will never be grounded.");
        }

        return changed;
    });
}

void InspectorPanel::drawScriptSection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    bool remove = false;
    const bool open = beginComponentCard(
        CardInfo<ScriptComponent>::TITLE,
        CardInfo<ScriptComponent>::accentColor(),
        true,
        &remove
    );

    // Move-only, so edits are recorded between serialized captures; skipped when nothing can edit.
    const std::string scriptBefore =
        (open || remove) ? ScriptEditCommand::capture(scene, id) : std::string();
    const auto pushScriptEdit = [&](const char* label) {
        std::string after = ScriptEditCommand::capture(scene, id);
        if (after == scriptBefore) return;
        state.pushStep(std::make_unique<ScriptEditCommand>(id, scriptBefore, std::move(after), label));
    };

    if (open) {
        auto& sc = scene.get<ScriptComponent>(id);

        // ScriptComponent serializes as one value, so an instance cannot hold field overrides.
        if (PrefabOverrides::instanceRoot(scene, id)) {
            ImGui::TextWrapped(
                "Script values on an instance belong to the prefab. Save as "
                "Prefab keeps what you change here; saving the scene does not."
            );
        }

        int removeIndex = -1;
        for (size_t i = 0; i < sc.behaviors.size(); ++i) {
            Behavior* behavior = sc.behaviors[i].get();
            if (!behavior) continue;
            ImGui::PushID(static_cast<int>(i));

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(EditorStyle::HEADER_TEXT, "%s", behavior->typeName());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - EditorStyle::px(14.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            const bool removeThis = ImGui::SmallButton("x##rmbeh");
            ImGui::PopStyleColor(2);
            if (removeThis) removeIndex = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove behavior");

            BehaviorFieldInspector inspector(m_behaviorEulers);
            behavior->visitFields(inspector);
            if (inspector.changed()) pushScriptEdit("Edit Behavior");

            ImGui::PopID();
            if (i + 1 < sc.behaviors.size()) ImGui::Separator();
        }

        if (sc.behaviors.empty() && sc.unknown.empty())
            ImGui::TextDisabled("No behaviors attached.");

        int removeUnknown = -1;
        // Its own scope: both lists push ids from 0, and an offset is clear only until one grows.
        ImGui::PushID("held");
        for (size_t i = 0; i < sc.unknown.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (!sc.behaviors.empty() || i > 0) ImGui::Separator();

            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(EditorStyle::WARNING, "%s", sc.unknown[i].type.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - EditorStyle::px(14.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            const bool dropThis = ImGui::SmallButton("x##rmunknown");
            ImGui::PopStyleColor(2);
            if (dropThis) removeUnknown = static_cast<int>(i);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Discard this behavior and its values");

            ImGui::TextDisabled(
                "No such behavior type is registered. Its values are "
                "kept and saved; it does not run."
            );
            ImGui::PopID();
        }
        ImGui::PopID();
        if (removeUnknown >= 0) {
            sc.unknown.erase(sc.unknown.begin() + removeUnknown);
            pushScriptEdit("Discard Missing Behavior");
        }

        ImGui::Spacing();
        if (ImGui::Button("+  Add Behavior", ImVec2(-1, 0))) ImGui::OpenPopup("##AddBehavior");
        if (ImGui::BeginPopup("##AddBehavior")) {
            const std::vector<std::string> names = BehaviorRegistry::get().names();
            if (names.empty()) {
                ImGui::TextDisabled("No behaviors registered.");
            } else {
                static char s_behaviorFilter[48] = {};
                popupSearchField(
                    "##behaviorFilter",
                    s_behaviorFilter,
                    sizeof(s_behaviorFilter),
                    EditorStyle::px(200.0f)
                );
                for (const std::string& name : names) {
                    if (!matchesFilter(name.c_str(), s_behaviorFilter)) continue;
                    if (ImGui::MenuItem(name.c_str())) {
                        if (auto behavior = BehaviorRegistry::get().create(name)) {
                            sc.behaviors.push_back(std::move(behavior));
                            pushScriptEdit("Add Behavior");
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (removeIndex >= 0) {
            sc.behaviors.erase(sc.behaviors.begin() + removeIndex);
            pushScriptEdit("Remove Behavior");
        }
    }
    endComponentCard();
    if (remove) {
        scene.remove<ScriptComponent>(id);
        pushScriptEdit("Remove Script");
        PrefabOverrides::warnComponentIsPrefabs(
            scene,
            state,
            id,
            "Script",
            "comes back from the prefab on the next load"
        );
    }
}

void InspectorPanel::drawHierarchySection(EditorContext& ec, EntityId id) {
    Scene& scene = ec.frame.scene;
    EditorState& state = ec.state;

    const bool open = beginComponentCard(
        CardInfo<Hierarchy>::TITLE,
        CardInfo<Hierarchy>::accentColor(),
        false
    );
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

        if (!unparented && h.firstChild) {
            ImGui::TextDisabled("Children:");
            HierarchyOperations::forEachChild(scene, id, [&](EntityId child) {
                char name[64];
                getEntityDisplayName(scene, child, name, sizeof(name));
                char cid[16];
                snprintf(cid, sizeof(cid), "%u", child.slot());
                const bool selected = state.selectedEntity == child;
                if (entitySelectable(cid, selected, entityIconKind(scene, child), name)) {
                    state.selectEntity(child);
                }
            });
        }
    }
    endComponentCard();
}

} // namespace Vkm::Engine
