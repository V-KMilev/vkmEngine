#pragma once

#include <imgui.h>

#include "base/container/storage_index.h"
#include "resource/asset_type.h"

#include "ui/style/editor_style.h"

namespace Vkm::Engine {

struct EditorContext;
class Resource;
class ResourceManager;

/**
 * @brief One asset's own window: the asset alone in a viewport of its own, and what it is.
 *
 * AssetEditors owns them, one per open asset. An editor knows its asset by handle, so one whose asset
 * is deleted is closed, never shown another; a replaced asset graph closes them all
 * (AssetEditors::forgetOtherGraph).
 */
class AssetEditor {
    public:
        /**
         * @brief An editor of the asset of kind @p type at @p key.
         *
         * @param type Its kind.
         * @param key  Its handle's slot and generation.
         */
        AssetEditor(AssetType type, StorageIndex key)
            : m_type(type)
            , m_key(key)
        {}
        virtual ~AssetEditor() = default;

        AssetEditor(const AssetEditor& other) = delete;
        AssetEditor& operator=(const AssetEditor& other) = delete;

        AssetEditor(AssetEditor && other) = delete;
        AssetEditor& operator=(AssetEditor && other) = delete;

    public:
        /**
         * @brief Its asset, while it is loaded.
         *
         * @param resources Where it is loaded.
         * @return The asset, or null once it has gone.
         */
        virtual const Resource* asset(const ResourceManager& resources) const = 0;

        /**
         * @brief Draw the window's content; called only while asset() is not null.
         *
         * @param ec The frame's editor context.
         */
        virtual void draw(EditorContext& ec) = 0;

    public:
        AssetType    type() const { return m_type; }
        StorageIndex key() const { return m_key; }

    private:
        AssetType    m_type;
        StorageIndex m_key;
};

inline constexpr float ASSET_DETAILS_WIDTH = 280.0f;  ///< The width of an asset window's details column,
                                                      ///< design pixels.

/**
 * @brief An asset window's layout: @p view filling the left, @p details down a column on the right.
 *
 * @param view    Draws the picture, in a child that neither scrolls nor takes the wheel.
 * @param details Draws the facts and controls.
 */
template <typename View, typename Details>
void viewAndDetails(View&& view, Details&& details) {
    const float side    = EditorStyle::px(ASSET_DETAILS_WIDTH);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float wide    = ImGui::GetContentRegionAvail().x - side - spacing;
    const ImGuiWindowFlags still = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("##view", ImVec2(wide, 0.0f), ImGuiChildFlags_None, still)) view();
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f))) details();
    ImGui::EndChild();
}

} // namespace Vkm::Engine
