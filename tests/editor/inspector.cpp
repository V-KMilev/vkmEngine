// The Inspector's cards as a person uses them, through ImGui with no window: what an edit of one
// selected entity carries to the rest, an instance's overrides marked on the rows that show them, and
// a row's label from its field's name, its tip over it.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/scene/transform.h"
#include "panels/inspector/inspector_panel.h"
#include "system/script/behavior.h"
#include "system/script/script_component.h"
#include "ui/field_label.h"
#include "ui/rows.h"

#include "editor/desk.h"
#include "harness/check.h"

namespace EditorInspector {

/**
 * @brief A behavior with a rotation, which its card shows as three angles.
 */
class Facing : public Vkm::Engine::Behavior<Facing> {
    public:
        glm::quat turn = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

} // namespace EditorInspector

VKM_BEHAVIOR(::EditorInspector::Facing)
    VKM_F(turn, "Which way it faces")
VKM_REFLECT_END()

using namespace Vkm::Test;
using EditorInspector::Facing;

TEST("Loop pressed on one of several Animators selected carries the loop, not that one's speed") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    std::vector<EntityId> rigs;
    for (const float speed : {2.0f, 1.0f}) {
        rigs.push_back(scene.createEntity());
        Animator animator;
        animator.speed = speed;
        scene.add(rigs.back(), animator);
        desk.state.selection.add(rigs.back());
    }
    desk.state.selection.makeActive(rigs.back());
    const bool looping = scene.get<Animator>(rigs.front()).looping;
    Pointer pointer;
    InspectorPanel panel;
    const auto draw = [&] { panel.draw(desk.ec); };

    // The Animator card's Loop, as a click on it.
    ImGuiID loop = 0;
    pointer.frame([&] {
        draw();
        ImGui::PushID("Animator");
        ImGui::PushID("rig");
        loop = ImGui::GetID("###loop");
        ImGui::PopID();
        ImGui::PopID();
    });
    ImGui::ActivateItemByID(loop);
    pointer.frame(draw);

    CHECK(scene.get<Animator>(rigs.front()).looping != looping);
    CHECK(scene.get<Animator>(rigs.front()).speed == 2.0f);
}

TEST("an instance's override of a list marks each of its rows, and a Revert asked on one reverts the list") {
    Pointer pointer;
    Collider collider;
    std::optional<std::string> asked;
    const auto partRows = [](Rows<ColliderPart>& part, size_t) { part.row<&ColliderPart::radius>(); };
    const auto draw = [&] {
        Rows<Collider> rows(collider, {"parts"}, nullptr);
        rows.each<&Collider::parts>(partRows);
        asked = rows.revertAsked();
    };
    // Inside the first part, where its Radius row keeps its menu.
    const auto inRadius = [](const auto& then) {
        ImGui::PushID(0);
        ImGui::PushID("Radius");
        then();
        ImGui::PopID();
        ImGui::PopID();
    };

    // The row's menu, opened as a right click on its label opens it; then its Revert to Prefab.
    pointer.frame([&] {
        draw();
        inRadius([] { ImGui::OpenPopupEx(ImGui::GetID("##rowMenu")); });
    });
    ImGuiID revert = 0;
    pointer.frame([&] {
        draw();
        inRadius([&] {
            if (!ImGui::BeginPopup("##rowMenu")) return;
            revert = ImGui::GetID("Revert to Prefab");
            ImGui::EndPopup();
        });
    });
    REQUIRE(revert != 0);
    ImGui::ActivateItemByID(revert);
    pointer.frame(draw);

    CHECK(asked == std::optional<std::string>("parts"));
}

TEST("a row is labeled by its field's name in words, and a field named for a color edits as one") {
    CHECK(fieldLabel("degreesPerSecond") == "Degrees Per Second");
    CHECK(fieldLabel("speed") == "Speed");
    // A run of capitals stays one word, and ends where a word begins.
    CHECK(fieldLabel("maxHPValue") == "Max HP Value");
    CHECK(fieldLabel("HDRColor") == "HDR Color");
    CHECK(fieldLabel("lod2Bias") == "Lod2 Bias");
    CHECK(fieldLabel("jump_height") == "Jump Height");
    // A shortened word is said in capitals, or in full.
    CHECK(fieldLabel("msaaSamples") == "MSAA Samples");
    CHECK(fieldLabel("fovY") == "FOV Y");
    CHECK(fieldLabel("valign") == "Vertical Align");
    CHECK(fieldLabel("fpsCap") == "FPS Cap");

    CHECK(namesAColor("tintColor"));
    CHECK(namesAColor("color"));
    CHECK(namesAColor("COLOR"));
    CHECK(!namesAColor("colorful"));
    CHECK(!namesAColor("velocity"));
}

namespace {

/// Whether @p tip shows somewhere over the label column of the selection's Inspector, the pointer moved
/// over it a point at a time, each frame's text read as ImGui logs it.
bool tipShows(Desk& desk, const std::string& tip) {
    Pointer pointer;
    InspectorPanel panel;
    pointer.frame([&] { panel.draw(desk.ec); });
    bool tipped = false;
    for (float y = 0.0f; y < Pointer::HEIGHT && !tipped; y += 4.0f) {
        for (float x = 4.0f; x < 100.0f && !tipped; x += 8.0f) {
            pointer.moveTo({x, y});
            pointer.frame([&] {
                ImGui::LogToBuffer();
                panel.draw(desk.ec);
                const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
                tipped = text.find(tip) != std::string::npos;
                ImGui::LogFinish();
            });
        }
    }
    return tipped;
}

} // namespace

TEST("a behavior's rotation shows its field's tip over its label, as its other fields do") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId id = scene.createEntity();
    addBehavior<Facing>(scene, id);
    desk.state.selection.select(id);
    CHECK(tipShows(desk, "Which way it faces"));
}

TEST("a bone socket's offset rows show the offset field's tip") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId id = scene.createEntity();
    scene.add(id, Transform{});
    scene.add(id, BoneSocket{});
    desk.state.selection.select(id);
    CHECK(tipShows(desk, "Where it sits on that bone"));
}
