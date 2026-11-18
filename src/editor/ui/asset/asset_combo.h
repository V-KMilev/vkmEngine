#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "undo/command_host.h"
#include "io/asset/asset_library.h"
#include "io/scene/scene_assets.h"
#include "resource/asset_type.h"
#include "resource/resource_manager.h"
#include "ui/asset/asset_drag.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

/**
 * @brief A property row picking which asset a handle points at, (none) included.
 *
 * Lists what is loaded, then, dimmed, the library's assets of the kind, which picking loads; a long
 * list opens with a search (searchCombo). An asset of the kind dropped on the row from the browser is
 * picked too.
 *
 * @param comboId ImGui id for the combo.
 * @param label Row label; null when the caller drew it.
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
    const std::string preview = current ? current->name() : std::string("(none)");
    if (label) drawPropertyLabel(label);
    ImGui::SetNextItemWidth(-1.0f);
    // What is loaded, then the library's names that are not: a null handle.
    std::vector<std::pair<Handle, std::string>> listed;
    const int chosen = searchCombo(comboId, preview.empty() ? "(unnamed)" : preview.c_str(), [&] {
        resources.forEachOfType<Asset>([&](Handle h, const Asset& a) {
            if (!a.isHidden()) listed.emplace_back(h, a.name());
        });
        for (const std::string& name : AssetLibrary::get().namesOf(ASSET_TYPE<Asset>)) {
            if (!resources.findByName<Asset>(name)) listed.emplace_back(Handle{}, name);
        }
        std::vector<ComboRow> rows;
        rows.reserve(listed.size() + 1);
        rows.push_back({"(none)", !currentHandle});
        for (const auto& [h, name] : listed) {
            const char* tip = h ? nullptr : "Not loaded yet: picking it loads it";
            rows.push_back({name.empty() ? "(unnamed)" : name, h && currentHandle == h, !h, 0, tip});
        }
        return rows;
    });

    bool picked = false;
    if (ImGui::BeginDragDropTarget()) {
        const std::optional<AssetDrag> drag = peekAssetDrag();
        if (drag && drag->type == ASSET_TYPE<Asset> && resources.tryGet(Handle{drag->key})) {
            if (ImGui::AcceptDragDropPayload(ASSET_DRAG) && currentHandle != Handle{drag->key}) {
                currentHandle = Handle{drag->key};
                picked        = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (chosen < 0) return picked;
    if (chosen == 0) {
        currentHandle = Handle{};
        return true;
    }
    const auto& [held, name] = listed[static_cast<size_t>(chosen) - 1];
    if (held) {
        currentHandle = held;
        return true;
    }
    // A library row is loaded once the list is closed: the load can move every name it held.
    if (!SceneAssets::loadNamed(ASSET_TYPE<Asset>, name, resources)) {
        host.pushToast(ToastKind::Error, "Could not load " + name);
        return picked;
    }
    currentHandle = resources.findByName<Asset>(name);
    return true;
}

} // namespace Vkm::Engine
