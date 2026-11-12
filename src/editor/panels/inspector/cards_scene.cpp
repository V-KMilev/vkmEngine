#include "panels/inspector/cards.h"

#include <imgui.h>

#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/hierarchy_operations.h"
#include "ui/editor_widgets.h"
namespace Vkm::Engine {

CardInfo cardInfo(Tag<Name>) {
    return {"Name", AddSection::Scene, nullptr, EditorIcon::None};
}

CardInfo cardInfo(Tag<Transform>) {
    return {"Transform", AddSection::Scene, "Where it is, how it is turned and its scale", EditorIcon::Move};
}

void drawCard(Rows<Transform>& c, CardContext& card) {
    // Shared by the axes moved (withMovedAxes), not by field; marked and reverted as any row.
    const auto axes = [](float speed) {
        return [speed](glm::vec3& value, const FieldInfo&) {
            return vec3Fields(&value.x, speed, 0.0f, 0.0f, "%.2f");
        };
    };
    c.rowWith<&Transform::position>(axes(0.1f));
    c.rowWith<&Transform::rotation>([&](glm::quat& rotation, const FieldInfo&) {
        EulerCache<EntityId>& euler = card.memory.transformEuler;
        euler.sync(card.id, rotation);
        if (!vec3Fields(euler.degrees(), 0.5f, 0.0f, 0.0f, "%.2f")) return false;
        rotation = euler.toQuat();
        return true;
    });
    c.rowWith<&Transform::scale>(axes(0.01f));

    const Hierarchy* node = card.scene.tryGet<Hierarchy>(card.id);
    if (node && node->parent) {
        glm::mat4 worldMat = HierarchyOperations::computeWorldMatrix(card.scene, card.id);
        glm::vec3 worldPos(worldMat[3]);
        ImGui::TextDisabled("World: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y, worldPos.z);
    }

    // A socket rewrites this every frame from its bone; say where the lasting value lives.
    if (card.scene.has<BoneSocket>(card.id)) cardNote(Note::Info, "Its Bone Socket sets it, by its Offset.");
}

} // namespace Vkm::Engine
