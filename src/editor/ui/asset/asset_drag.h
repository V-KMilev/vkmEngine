#pragma once

#include <cstring>
#include <optional>

#include <imgui.h>

#include "base/container/storage_index.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

inline constexpr const char* ASSET_DRAG = "VKM_ASSET";  ///< The ImGui payload type an asset dragged out of
                                                        ///< the browser carries.

/**
 * @brief An asset being dragged, its kind and key; plain data, copied whole by ImGui.
 */
struct AssetDrag {
    AssetType    type;
    StorageIndex key;
};

/**
 * @brief The asset the drag in flight carries, read without taking it.
 *
 * A drop target accepts ASSET_DRAG only for a kind it takes, so another kind is neither lit nor
 * swallowed.
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
