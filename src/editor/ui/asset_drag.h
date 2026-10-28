#pragma once

#include <cstring>
#include <optional>

#include <imgui.h>

#include "core/memory/types.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

/// The ImGui payload type an asset dragged out of the browser carries.
inline constexpr const char* ASSET_DRAG = "VKM_ASSET";

/**
 * @brief An asset being dragged: which kind, and which one of it.
 *
 * Plain data, copied whole by ImGui.
 */
struct AssetDrag {
    AssetType    type;
    StorageIndex key;
};

/**
 * @brief The asset the drag in flight carries, read without taking it.
 *
 * A drop target peeks, asks whether it takes that kind, and only then accepts ASSET_DRAG, so a
 * drag of another kind is neither lit nor swallowed.
 *
 * @return The drag, or none when what is dragged is no asset.
 */
inline std::optional<AssetDrag> peekAssetDrag() {
    const ImGuiPayload* peek = ImGui::GetDragDropPayload();
    if (!peek || !peek->IsDataType(ASSET_DRAG) || peek->DataSize != sizeof(AssetDrag)) return std::nullopt;
    AssetDrag drag{};
    std::memcpy(&drag, peek->Data, sizeof(drag));
    return drag;
}

} // namespace Vkm::Engine
