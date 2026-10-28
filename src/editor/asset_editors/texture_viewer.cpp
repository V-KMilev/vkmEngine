#include "asset_editors/texture_viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "core/reflect.h"
#include "core/system.h"
#include "editor_context.h"
#include "platform/window/window_manager.h"
#include "resource/asset/texture_asset.h"
#include "resource/resource_manager.h"
#include "resource/texture_format.h"
#include "system/render/editor_render_hooks.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/// The side of a checkerboard square behind the texture, design pixels.
constexpr float CHECKER = 12.0f;

/// How much nearer a notch of the wheel brings the texture.
constexpr float ZOOM_STEP = 1.2f;

/// The least and most screen coordinates a texel is drawn at.
constexpr float LEAST_ZOOM = 1.0f / 64.0f;
constexpr float MOST_ZOOM  = 64.0f;

/// The channel buttons' names, red to blue, and what each shows or hides.
constexpr const char* CHANNELS[]     = {"R", "G", "B"};
constexpr const char* CHANNEL_TIPS[] = {"Show or hide red", "Show or hide green", "Show or hide blue"};

} // namespace

TextureViewer::TextureViewer(StorageIndex key)
    : AssetEditor(AssetType::Texture, key)
{}

const Resource* TextureViewer::asset(const ResourceManager& resources) const {
    return resources.tryGet(TextureHandle{key()});
}

void TextureViewer::draw(EditorContext& ec) {
    const auto view = [&] {
        const ImVec2 inside = ImGui::GetContentRegionAvail();
        drawPicture(ec, glm::vec2(inside.x, inside.y));
    };
    viewAndDetails(view, [&] { drawDetails(ec); });
}

void TextureViewer::drawPicture(EditorContext& ec, glm::vec2 room) {
    if (room.x < EditorStyle::px(8.0f) || room.y < EditorStyle::px(8.0f)) return;
    const ResourceManager& resources = ec.frame.resources;
    const TextureAsset&    texture   = resources.get(TextureHandle{key()});
    const glm::vec2 texels(
        static_cast<float>(std::max(texture.params.width, 1u)),
        static_cast<float>(std::max(texture.params.height, 1u))
    );

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImGuiButtonFlags buttons = ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle;
    ImGui::InvisibleButton("##picture", ImVec2(room.x, room.y), buttons);
    const bool     hovered = ImGui::IsItemHovered();
    const ImGuiIO& io      = ImGui::GetIO();

    const float fit  = std::min(room.x / texels.x, room.y / texels.y);
    float       zoom = m_zoom > 0.0f ? m_zoom : fit;
    const glm::vec2 middle(origin.x + room.x * 0.5f, origin.y + room.y * 0.5f);
    if (hovered && io.MouseWheel != 0.0f) {
        // About the cursor: the texel under it stays under it.
        const float     next   = std::clamp(zoom * std::pow(ZOOM_STEP, io.MouseWheel), LEAST_ZOOM, MOST_ZOOM);
        const glm::vec2 cursor = glm::vec2(io.MousePos.x, io.MousePos.y) - middle;
        m_offset = cursor + (m_offset - cursor) * (next / zoom);
        zoom     = next;
        m_zoom   = next;
    }
    if (ImGui::IsItemActive()) m_offset += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_zoom   = 0.0f;
        m_offset = glm::vec2(0.0f);
        zoom     = fit;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 corner(origin.x + room.x, origin.y + room.y);
    dl->PushClipRect(origin, corner, true);
    const glm::vec2 drawn  = texels * zoom;
    const glm::vec2 lowest = middle + m_offset - drawn * 0.5f;
    const glm::vec2 lo     = glm::max(lowest, glm::vec2(origin.x, origin.y));
    const glm::vec2 hi     = glm::min(lowest + drawn, glm::vec2(corner.x, corner.y));

    // The checkerboard under it, where it is drawn, so its transparency shows.
    const float square = EditorStyle::px(CHECKER);
    const ImU32 light  = IM_COL32(110, 110, 116, 255);
    const ImU32 dark   = IM_COL32(78, 78, 84, 255);
    for (float y = lo.y; y < hi.y; y += square) {
        for (float x = lo.x; x < hi.x; x += square) {
            const auto column = static_cast<int>((x - lowest.x) / square);
            const auto row    = static_cast<int>((y - lowest.y) / square);
            const ImVec2 to(std::min(x + square, hi.x), std::min(y + square, hi.y));
            dl->AddRectFilled(ImVec2(x, y), to, ((column + row) & 1) ? light : dark);
        }
    }

    const GpuTextureId gpu = ec.hooks.ensureTexture(TextureHandle{key()}, resources);
    if (gpu && !texture.loading) {
        const ImU32 tint = IM_COL32(
            m_channels[0] ? 255 : 0,
            m_channels[1] ? 255 : 0,
            m_channels[2] ? 255 : 0,
            255
        );
        // Texture rows run bottom-up, as GL stores them.
        const ImVec2 from(lowest.x, lowest.y);
        const ImVec2 to(lowest.x + drawn.x, lowest.y + drawn.y);
        dl->AddImage(imTexture(gpu), from, to, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
    }
    dl->PopClipRect();

    if (hovered && !ImGui::IsItemActive()) {
        const glm::vec2 cursor = (glm::vec2(io.MousePos.x, io.MousePos.y) - lowest) / zoom;
        if (cursor.x >= 0.0f && cursor.y >= 0.0f && cursor.x < texels.x && cursor.y < texels.y) {
            ImGui::SetTooltip(
                "Texel %d, %d at %.0f%%\nWheel to zoom, drag to pan, double-click to fit",
                static_cast<int>(cursor.x),
                static_cast<int>(cursor.y),
                static_cast<double>(zoom * ec.frame.window.framebufferScale() * 100.0f)
            );
        }
    }
}

void TextureViewer::drawDetails(EditorContext& ec) {
    const TextureAsset& texture = ec.frame.resources.get(TextureHandle{key()});
    const TextureParams& params = texture.params;

    sectionLabel("Image");
    char line[96];
    if (texture.loading) {
        factRow("Size", "Decoding...");
    } else {
        std::snprintf(line, sizeof(line), "%u x %u", params.width, params.height);
        factRow("Size", line);
    }
    std::snprintf(line, sizeof(line), "%u", channelCount(params.format));
    factRow("Channels", line);
    factRow("Holds", Reflect::enumName(texture.usage()));
    factRow("Stored", isCompressedFormat(params.internalFormat) ? "Block-compressed" : "Uncompressed");
    std::snprintf(line, sizeof(line), "%u", params.mipLevels);
    factRow("Mip levels", line);
    const double megabytes = static_cast<double>(texture.pixelData.size()) / (1024.0 * 1024.0);
    std::snprintf(line, sizeof(line), "%.2f MB", megabytes);
    factRow("In memory", line);

    ImGui::Spacing();
    sectionLabel("Show");
    for (size_t c = 0; c < 3; ++c) {
        if (c > 0) ImGui::SameLine();
        const bool shown = m_channels[c];
        if (shown) ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
        if (ImGui::Button(CHANNELS[c], ImVec2(EditorStyle::px(36.0f), 0.0f))) m_channels[c] = !shown;
        if (shown) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", CHANNEL_TIPS[c]);
    }
    if (ImGui::Button("Fit", ImVec2(-1.0f, 0.0f))) {
        m_zoom   = 0.0f;
        m_offset = glm::vec2(0.0f);
    }
    // A texel to a framebuffer pixel, which a HiDPI screen coordinate holds several of.
    if (ImGui::Button("Actual size", ImVec2(-1.0f, 0.0f))) {
        m_zoom   = 1.0f / ec.frame.window.framebufferScale();
        m_offset = glm::vec2(0.0f);
    }
}

} // namespace Vkm::Engine
