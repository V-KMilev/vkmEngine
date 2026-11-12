#include "panels/inspector/cards.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>

#include "undo/component_commands.h"
#include "undo/prefab_overrides.h"
#include "ecs/scene.h"
#include "actions/prefab_actions.h"
#include "editor_state.h"
#include "io/asset/asset_library.h"
#include "io/project/roots.h"
#include "ecs/prefab_instances.h"
#include "system/script/behavior_base.h"
#include "system/script/behavior_field_visitor.h"
#include "base/reflect/field_info.h"
#include "system/script/script_component.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"
#include "ui/field_label.h"

namespace Vkm::Engine {

namespace {

/// The rows of @p names, (none) first, the one named @p current selected.
std::vector<ComboRow> nameRows(const std::vector<std::string>& names, const std::string& current) {
    std::vector<ComboRow> rows;
    rows.reserve(names.size() + 1);
    rows.push_back({"(none)", current.empty()});
    for (const std::string& name : names) rows.push_back({name, name == current});
    return rows;
}

/**
 * @brief A row picking a name from @p names, (none) included.
 *
 * @param info    The field.
 * @param current The name, written on a pick.
 * @param names   Asked for only while the list is open.
 * @return Whether a pick changed it.
 */
template <typename Names>
bool nameRow(const FieldInfo& info, std::string& current, Names&& names) {
    const std::string label = fieldLabel(info.name);
    drawPropertyLabel(label.c_str());
    ImGui::PushID(info.name);
    std::vector<std::string> listed;
    const int picked = searchCombo("##v", current.empty() ? "(none)" : current.c_str(), [&] {
        listed = names();
        return nameRows(listed, current);
    });
    if (info.tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.tip);
    ImGui::PopID();
    if (picked < 0) return false;
    current = picked == 0 ? std::string() : listed[static_cast<size_t>(picked) - 1];
    return true;
}

// A behavior's authored fields, through the visitor serialization uses, as a card's rows: a row's
// label is fieldLabel(name), the name staying the saved key.
class BehaviorFieldInspector : public BehaviorFieldVisitor {
    public:
        static constexpr const char* PLACES = "%.3f";  ///< A behavior's numbers show three places: its
                                                       ///< fields carry no format of their own.

    public:
        explicit BehaviorFieldInspector(CardMemory& memory) : m_memory(memory) {}
        ~BehaviorFieldInspector() override = default;

        BehaviorFieldInspector(const BehaviorFieldInspector& other) = delete;
        BehaviorFieldInspector& operator=(const BehaviorFieldInspector& other) = delete;

        BehaviorFieldInspector(BehaviorFieldInspector && other) = delete;
        BehaviorFieldInspector& operator=(BehaviorFieldInspector && other) = delete;

    public:
        void field(const FieldInfo& info, float& v) override { m_changed |= editValue(info, v, PLACES); }
        void field(const FieldInfo& info, int& v) override { m_changed |= editValue(info, v); }
        void field(const FieldInfo& info, bool& v) override { m_changed |= editValue(info, v); }
        void field(const FieldInfo& info, glm::vec2& v) override { m_changed |= editValue(info, v, PLACES); }
        void field(const FieldInfo& info, glm::vec3& v) override { m_changed |= editValue(info, v, PLACES); }
        void field(const FieldInfo& info, glm::vec4& v) override { m_changed |= editValue(info, v, PLACES); }
        void field(const FieldInfo& info, std::string& v) override { m_changed |= editValue(info, v); }

        // A cache per field, so a typed axis is not re-derived and snapped next frame.
        void field(const FieldInfo& info, glm::quat& v) override {
            EulerCache<int>& euler = m_memory.behaviorEulers[&v];
            euler.sync(0, v);
            const std::string label = fieldLabel(info.name);
            if (drawVec3Control(label.c_str(), info.tip, euler.degrees(), 0.0f, 0.5f)) {
                v         = euler.toQuat();
                m_changed = true;
            }
        }

        void enumField(
            const FieldInfo& info,
            int& index,
            const char* const* names,
            std::size_t count
        ) override {
            const std::string label = fieldLabel(info.name);
            const auto combo = [&] { return comboList("##v", &index, names, static_cast<int>(count)); };
            m_changed |= propRow(label.c_str(), info.tip, combo);
        }

        // The project's library, not what is loaded: the name goes into the scene's assets block on
        // save, which is what loads it.
        void assetField(const FieldInfo& info, std::string& name, AssetType type) override {
            m_changed |= nameRow(info, name, [&] { return AssetLibrary::get().namesOf(type); });
            // Not in the library (deleted, or from another project): the next load fails.
            if (!name.empty() && !AssetLibrary::get().find(type, name)) {
                cardNote(Note::Danger, "Not in this project's library.");
            }
        }

        // The project's prefabs, walked as the list opens; the path is what the scene saves.
        void prefabField(const FieldInfo& info, std::string& path) override {
            m_changed |= nameRow(info, path, [&] {
                if (ImGui::IsWindowAppearing()) m_memory.prefabs = EditorActions::listPrefabs();
                return m_memory.prefabs;
            });
            std::error_code missing;
            if (!path.empty() && !std::filesystem::exists(Roots::resolveProjectPath(path), missing)) {
                cardNote(Note::Danger, "No prefab there: nothing is built.");
            }
        }

        bool beginStruct(const FieldInfo& info) override {
            const std::string fold = fieldLabel(info.name) + "###" + info.name;
            return beginFold(fold.c_str(), true);
        }
        void endStruct() override { endFold(); }

        bool changed() const { return m_changed; }

    private:
        CardMemory& m_memory;
        bool        m_changed = false;
};

} // namespace

CardInfo cardInfo(Tag<ScriptComponent>) {
    return {"Script", AddSection::Behaviors, nullptr, EditorIcon::Script};
}

void drawScriptCard(CardContext& card) {
    Scene& scene = card.scene;
    EditorState& state = card.state;
    const EntityId id = card.id;

    bool remove = false;
    const CardInfo info = cardInfo(Tag<ScriptComponent>{});
    const bool open = beginComponentCard(info.title, info.icon, true, "##ScriptMenu");
    if (ImGui::BeginPopup("##ScriptMenu")) {
        if (ImGui::MenuItem("Remove Component")) remove = true;
        ImGui::EndPopup();
    }

    // Move-only, so edits are recorded between serialized captures; skipped when nothing can edit.
    // Taken every frame in play, whose behaviors change themselves.
    const uint64_t entity = (static_cast<uint64_t>(id.generation()) << 32) | id.slot();
    const std::array<uint64_t, 3> key = {entity, scene.epoch(), state.commands.revision()};
    if ((open || remove) && (card.memory.scriptKey != key || card.playing)) {
        card.memory.scriptBefore = ScriptEditCommand::capture(scene, id);
        card.memory.scriptKey    = key;
    }
    const std::string& before = card.memory.scriptBefore;
    const auto pushScriptEdit = [&](const char* label) {
        std::string after = ScriptEditCommand::capture(scene, id);
        if (after == before) return;
        state.pushStep(std::make_unique<ScriptEditCommand>(id, before, std::move(after), label));
    };

    if (open) {
        ScriptComponent& script = scene.get<ScriptComponent>(id);
        // ScriptComponent serializes as one value, so an instance cannot hold field overrides.
        if (Prefab::instanceRootOf(scene, id)) {
            cardNote(Note::Info, "An instance's script values are its prefab's: Apply to Prefab keeps them.");
        }

        int removeIndex = -1;
        for (size_t i = 0; i < script.behaviors.size(); ++i) {
            BehaviorBase* behavior = script.behaviors[i].get();
            if (!behavior) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            sectionLabel(behavior->typeName());
            if (removeButton("remove", "Remove behavior")) removeIndex = static_cast<int>(i);
            BehaviorFieldInspector inspector(card.memory);
            behavior->visitFields(inspector);
            if (inspector.changed()) pushScriptEdit("Edit Behavior");
            ImGui::PopID();
        }
        if (script.behaviors.empty() && script.unknown.empty()) cardNote(Note::Info, "No behaviors.");

        int removeUnknown = -1;
        // Its own scope: both lists push ids from 0, and an offset is clear only until one grows.
        ImGui::PushID("held");
        for (size_t i = 0; i < script.unknown.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            sectionLabel(script.unknown[i].type.c_str());
            if (removeButton("remove", "Discard this behavior and its values")) {
                removeUnknown = static_cast<int>(i);
            }
            cardNote(Note::Warning, "No such behavior is registered: its values are kept; it does not run.");
            ImGui::PopID();
        }
        ImGui::PopID();
        if (removeUnknown >= 0) {
            script.unknown.erase(script.unknown.begin() + removeUnknown);
            pushScriptEdit("Discard Missing Behavior");
        }
        if (removeIndex >= 0) {
            removeBehavior(script, static_cast<size_t>(removeIndex));
            pushScriptEdit("Remove Behavior");
        }
    }
    endComponentCard();
    if (remove) {
        scene.remove<ScriptComponent>(id);
        pushScriptEdit("Remove Script");
        const char* fate = "comes back from the prefab on the next load";
        PrefabOverrides::warnComponentIsPrefabs(scene, state, id, "Script", fate);
    }
}

} // namespace Vkm::Engine
