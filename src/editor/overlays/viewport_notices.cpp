#include "overlays/viewport_notices.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/frame_context.h"
#include "editor_context.h"
#include "editor_state.h"
#include "input/editor_keybinds.h"
#include "ui/style/editor_style.h"
#include "core/frame/loading_frame.h"
#include "core/frame/render_readiness.h"
#include "core/frame/visibility.h"
#include "view/camera_controller_system.h"
#include "overlays/overlay_strip.h"

namespace Vkm::Engine {

void drawFlySpeed(EditorContext& ec, const CameraControllerSystem& camera) {
    const float shown = camera.speedShownFor();
    if (shown <= 0.0f) return;

    char text[64];
    const float speed = camera.getSettings().moveSpeed * camera.speedScale();
    snprintf(text, sizeof(text), "Fly speed %.2g m/s (x%.2g)", speed, camera.speedScale());

    // The last half second fades.
    const float  alpha = std::min(shown * 2.0f, 1.0f);
    const ImVec2 size  = ImGui::CalcTextSize(text);
    const float  pad   = EditorStyle::px(8.0f);
    const ImVec2 at(
        ec.viewportPos.x + (ec.viewportSize.x - size.x) * 0.5f,
        ec.viewportPos.y + ec.viewportSize.y - size.y - pad * 2.0f - OverlayStrip::inset()
    );
    ImVec4 bg = EditorStyle::OVERLAY_BG;
    bg.w *= alpha;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(at.x - pad, at.y - pad),
        ImVec2(at.x + size.x + pad, at.y + size.y + pad),
        ImGui::GetColorU32(bg),
        EditorStyle::px(6.0f)
    );
    dl->AddText(at, ImGui::GetColorU32(ImGuiCol_Text, alpha), text);
}

void drawLoadingNotice(EditorContext& ec) {
    const FrameContext& ctx = ec.frame;
    if (!ctx.loading || ctx.loading->progress >= 1.0f || ctx.loading->status.empty()) return;
    const bool blank = ctx.readiness.compiled < ctx.readiness.programs;

    const char*  text = ctx.loading->status.c_str();
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float  pad  = EditorStyle::px(10.0f);
    const float  barH = EditorStyle::px(4.0f);
    const float  wide = std::max(size.x, EditorStyle::px(240.0f));
    const float  tall = size.y + pad + barH;
    const ImVec2 at(
        ec.viewportPos.x + (ec.viewportSize.x - wide) * 0.5f,
        blank
            ? ec.viewportPos.y + (ec.viewportSize.y - tall) * 0.5f
            : ec.viewportPos.y + ec.viewportSize.y - tall - pad * 2.0f - OverlayStrip::inset()
    );

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(at.x - pad, at.y - pad),
        ImVec2(at.x + wide + pad, at.y + tall + pad),
        ImGui::GetColorU32(EditorStyle::OVERLAY_BG),
        EditorStyle::px(6.0f)
    );
    dl->AddText(ImVec2(at.x + (wide - size.x) * 0.5f, at.y), ImGui::GetColorU32(ImGuiCol_Text), text);
    const ImVec2 track(at.x, at.y + size.y + pad);
    dl->AddRectFilled(track, ImVec2(track.x + wide, track.y + barH), ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddRectFilled(
        track,
        ImVec2(track.x + wide * std::clamp(ctx.loading->progress, 0.0f, 1.0f), track.y + barH),
        ImGui::GetColorU32(EditorStyle::ACCENT)
    );
}

void drawNoCameraNotice(EditorContext& ec) {
    const FrameContext& ctx = ec.frame;
    if (ctx.visibility && ctx.visibility->hasCamera) return;

    // Only a session showing the game gets here; the editor's view always has a camera.
    const char* headline = "No active camera - the game has nothing to render from";
    char detail[160];
    snprintf(
        detail,
        sizeof(detail),
        "Entity > Create > Camera, or tick Active on a Camera card. %s ejects to the editor's view.",
        keyLabel(ec.state.prefs.keybinds.ejectView).buf
    );

    const ImVec2 headSize   = ImGui::CalcTextSize(headline);
    const ImVec2 detailSize = ImGui::CalcTextSize(detail);
    const float lineGap = ImGui::GetTextLineHeightWithSpacing() - ImGui::GetTextLineHeight();
    const float blockH  = headSize.y + lineGap + detailSize.y;
    const ImVec2 center(
        ec.viewportPos.x + ec.viewportSize.x * 0.5f,
        ec.viewportPos.y + ec.viewportSize.y * 0.5f
    );

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 blockMin(center.x - std::max(headSize.x, detailSize.x) * 0.5f, center.y - blockH * 0.5f);
    const float padX = EditorStyle::px(14.0f);
    const float padY = EditorStyle::px(12.0f);
    const ImVec2 panelMin(blockMin.x - padX, blockMin.y - padY);
    const ImVec2 panelMax(blockMin.x + std::max(headSize.x, detailSize.x) + padX, blockMin.y + blockH + padY);
    dl->AddRectFilled(panelMin, panelMax, ImGui::GetColorU32(EditorStyle::OVERLAY_BG), EditorStyle::px(6.0f));
    dl->AddText(
        ImVec2(center.x - headSize.x * 0.5f, blockMin.y),
        ImGui::GetColorU32(EditorStyle::WARNING),
        headline
    );
    dl->AddText(
        ImVec2(center.x - detailSize.x * 0.5f, blockMin.y + headSize.y + lineGap),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        detail
    );
}

} // namespace Vkm::Engine
