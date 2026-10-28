#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "command/command_host.h"
#include "io/asset/asset_library.h"
#include "io/asset/asset_serializer.h"
#include "resource/asset_type.h"
#include "resource/resource_manager.h"
#include "ui/asset_drag.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

/// Past this many assets a picker's list opens with a search box.
inline constexpr size_t ASSET_SEARCH_FROM = 8;

/**
 * @brief A property row picking which asset a handle points at, (none) included.
 *
 * Lists what is loaded, then, dimmed, the library's assets of the kind that are not, which
 * picking loads. The list is snapshotted, so ImGuiListClipper can page through thousands; a long
 * one opens with a search box. An asset of the kind dragged out of the browser and dropped on the
 * row is picked too.
 *
 * @param comboId ImGui id for the combo.
 * @param label Row label.
 * @param resources Lists the loaded assets of the kind, and loads a library one picked.
 * @param host Takes the toast when a library asset picked will not load.
 * @param currentHandle Written when a row is chosen.
 * @return Whether the handle changed.
 */
template <typename Asset, typename Handle>
bool pickAsset(
    const char* comboId,
    const char* label,
    ResourceManager& resources,
    CommandHost& host,
    Handle& currentHandle
) {
    const Asset* current = resources.tryGet(currentHandle);
    const std::string cur = current ? current->name() : std::string("(none)");
    drawPropertyLabel(label);
    ImGui::SetNextItemWidth(-1.0f);
    const bool open = beginCombo(comboId, cur.empty() ? "(unnamed)" : cur.c_str());

    bool picked = false;
    if (ImGui::BeginDragDropTarget()) {
        const std::optional<AssetDrag> drag = peekAssetDrag();
        if (drag && drag->type == ASSET_TYPE<Asset> && resources.tryGet(Handle{drag->key})) {
            if (ImGui::AcceptDragDropPayload(ASSET_DRAG) && currentHandle != Handle{drag->key}) {
                currentHandle = Handle{drag->key};
                picked = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (!open) return picked;

    // What is loaded, then the library's names that are not: a null handle.
    std::vector<std::pair<Handle, const std::string*>> rows;
    resources.forEachOfType<Asset>([&](Handle h, const Asset& a) {
        if (a.isHidden()) return;
        rows.emplace_back(h, &a.name());
    });
    const std::vector<std::string> library = AssetLibrary::get().namesOf(ASSET_TYPE<Asset>);
    for (const std::string& name : library) {
        if (!resources.findByName<Asset>(name)) rows.emplace_back(Handle{}, &name);
    }

    // One list is open at a time, so one search per kind of asset serves.
    static char s_search[64] = {};
    if (rows.size() > ASSET_SEARCH_FROM) {
        popupSearchField("##search", s_search, sizeof(s_search));
        if (s_search[0] != '\0') {
            const auto missing = [](const std::pair<Handle, const std::string*>& row) {
                return !matchesFilter(row.second->c_str(), s_search);
            };
            rows.erase(std::remove_if(rows.begin(), rows.end(), missing), rows.end());
        }
    }

    if (ImGui::Selectable("(none)", !currentHandle)) {
        currentHandle = Handle{};
        picked = true;
    }

    // A library row picked is loaded after the list: the load can move every name the rows point at.
    std::string load;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& [h, name] = rows[i];
            ImGui::PushID(i);
            const bool  held  = static_cast<bool>(h);
            const char* shown = name->empty() ? "(unnamed)" : name->c_str();
            if (!held) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            const bool chosen = ImGui::Selectable(shown, held && currentHandle == h);
            if (!held) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not loaded yet: picking it loads it");
            }
            if (chosen && held) {
                currentHandle = h;
                picked        = true;
            } else if (chosen) {
                load = *name;
            }
            ImGui::PopID();
        }
    }
    if (!load.empty()) {
        if (AssetSerializer::loadNamed(ASSET_TYPE<Asset>, load, resources)) {
            currentHandle = resources.findByName<Asset>(load);
            picked        = true;
        } else {
            host.pushToast(ToastKind::Error, "Could not load " + load);
        }
    }
    ImGui::EndCombo();
    return picked;
}

} // namespace Vkm::Engine
