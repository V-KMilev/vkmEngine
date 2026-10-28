#include "ui/preview_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

#include <imgui.h>

#include "core/fnv1a.h"
#include "core/system.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "platform/window/window_manager.h"
#include "session/material_preview_session.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/// Degrees the view turns for a pixel the mouse drags.
constexpr float ORBIT_DEGREES_PER_PIXEL = 0.4f;

/// How much nearer a notch of the wheel brings the view.
constexpr float ZOOM_STEP = 0.88f;

/// The nearest and farthest the view comes, in the draws' bounding radii.
constexpr float NEAREST  = 0.3f;
constexpr float FARTHEST = 60.0f;

/// What each backdrop is called on the strip, in PreviewBackground's order.
constexpr const char* BACKDROPS[] = {"Dark", "Grey", "Sky"};
static_assert(
    std::size(BACKDROPS) == static_cast<size_t>(PreviewBackground::Count),
    "A name for each backdrop"
);

} // namespace

PreviewViewport::PreviewViewport(uint64_t key)
    : m_key(key)
{}

void PreviewViewport::frame() {
    m_yaw      = YAW;
    m_pitch    = PITCH;
    m_distance = DISTANCE;
    m_pan      = glm::vec2(0.0f);
}

void PreviewViewport::draw(
    EditorContext& ec,
    const std::vector<PreviewDraw>& draws,
    uint64_t version,
    glm::vec2 size
) {
    const ImVec2 room = ImGui::GetContentRegionAvail();
    const ImVec2 extent(size.x > 0.0f ? size.x : room.x, size.y > 0.0f ? size.y : room.y);
    if (extent.x < EditorStyle::px(8.0f) || extent.y < EditorStyle::px(8.0f)) return;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + extent.x, origin.y + extent.y);

    // The mouse first, so the picture this frame shows its answer. The strip drawn after it
    // takes the clicks over it.
    ImGui::SetNextItemAllowOverlap();
    const ImGuiButtonFlags buttons = ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
        | ImGuiButtonFlags_MouseButtonMiddle;
    ImGui::InvisibleButton("##preview", extent, buttons);
    const bool    hovered = ImGui::IsItemHovered();
    const ImGuiIO& io     = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        const bool panning = ImGui::IsMouseDown(ImGuiMouseButton_Middle)
            || (io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        if (panning) {
            // The picture's height spans this many bounding radii at the centre's depth.
            const float half  = glm::radians(PreviewRequest::FIELD_DEGREES * 0.5f);
            const float span  = 2.0f * m_distance * std::tan(half);
            const float scale = span / extent.y;
            m_pan.x -= io.MouseDelta.x * scale;
            m_pan.y += io.MouseDelta.y * scale;
        } else {
            m_yaw  -= io.MouseDelta.x * ORBIT_DEGREES_PER_PIXEL;
            m_pitch = std::clamp(m_pitch + io.MouseDelta.y * ORBIT_DEGREES_PER_PIXEL, -89.0f, 89.0f);
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        m_distance = std::clamp(m_distance * std::pow(ZOOM_STEP, io.MouseWheel), NEAREST, FARTHEST);
    }
    const bool reframe = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
        || isPressed(ec.state.prefs.keybinds.focusSelected, false);
    if (hovered && reframe) frame();
    if (hovered) ec.state.previewHovered = true;

    const float scale = ec.frame.window.framebufferScale();
    PreviewRequest req;
    req.key         = m_key;
    req.width       = static_cast<uint32_t>(std::max(1L, std::lround(extent.x * scale)));
    req.height      = static_cast<uint32_t>(std::max(1L, std::lround(extent.y * scale)));
    req.draws       = draws;
    req.yawDeg      = m_yaw;
    req.pitchDeg    = m_pitch;
    req.distance    = m_distance;
    req.pan         = m_pan;
    req.background  = m_background;
    req.lightYawDeg = m_lightYaw;

    // The view is part of the picture, as what it draws is.
    const float view[] = {
        m_yaw,
        m_pitch,
        m_distance,
        m_pan.x,
        m_pan.y,
        m_lightYaw,
        static_cast<float>(m_background),
        static_cast<float>(req.width),
        static_cast<float>(req.height)
    };
    const uint64_t stamp   = fnv1a64Bytes(view, sizeof(view), version);
    const bool     live    = true;
    const GpuTextureId tex = ec.materialPreviews.texture(ec.frame.resources, req, stamp, live);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tex) {
        dl->AddImage(imTexture(tex), origin, corner, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        dl->AddRectFilled(origin, corner, ImGui::GetColorU32(ImGuiCol_FrameBg));
        const char*  waiting = "Preparing the preview";
        const ImVec2 words   = ImGui::CalcTextSize(waiting);
        dl->AddText(
            ImVec2(origin.x + (extent.x - words.x) * 0.5f, origin.y + (extent.y - words.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            waiting
        );
    }
    if (hovered && !ImGui::IsItemActive()) {
        ImGui::SetItemTooltip("Drag to orbit, middle-drag or Shift-drag to pan, wheel to zoom, F to frame");
    }

    drawStrip(glm::vec2(origin.x, origin.y));
    ImGui::SetCursorScreenPos(ImVec2(origin.x, corner.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void PreviewViewport::drawStrip(glm::vec2 corner) {
    const float pad = EditorStyle::px(6.0f);
    ImGui::SetCursorScreenPos(ImVec2(corner.x + pad, corner.y + pad));
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::OVERLAY_BG);
    const auto backdrop = static_cast<size_t>(m_background);
    if (ImGui::SmallButton(BACKDROPS[backdrop])) {
        const auto count = static_cast<size_t>(PreviewBackground::Count);
        m_background = static_cast<PreviewBackground>((backdrop + 1) % count);
    }
    ImGui::SetItemTooltip("Backdrop: click for the next");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(EditorStyle::px(96.0f));
    sliderFloat("##light", &m_lightYaw, -180.0f, 180.0f, "Light %.0f deg");
    ImGui::SetItemTooltip("Swing the light round the preview");
    ImGui::PopStyleColor();
}

} // namespace Vkm::Engine
