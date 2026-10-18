#pragma once

#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "resource/resource_manager.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

/**
 * @brief A property row picking which loaded asset a handle points at, (none) included.
 *
 * The list is snapshotted, so ImGuiListClipper can page through thousands.
 *
 * @param comboId ImGui id for the combo.
 * @param label Row label.
 * @param resources Lists the loaded assets of the kind.
 * @param currentHandle Written when a row is chosen.
 * @return Whether the handle changed.
 */
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

} // namespace Vkm::Engine
