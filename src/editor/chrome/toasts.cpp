#include "chrome/toasts.h"

#include <algorithm>
#include <cstddef>

#include <imgui.h>

#include "chrome/editor_status_bar.h"
#include "ui/style/editor_icons.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

// Dark enough for white text.
const ImVec4 TOAST_ERROR_BG   = ImVec4(0.55f, 0.18f, 0.18f, 1.00f);
const ImVec4 TOAST_WARNING_BG = ImVec4(0.55f, 0.42f, 0.10f, 1.00f);
const ImVec4 TOAST_INFO_BG    = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);

} // namespace

std::optional<ConsoleView> Toasts::draw(float deltaTime) {
    for (Toast& toast : m_shown) {
        if (!m_hovered) toast.remaining -= deltaTime;
    }
    const auto spent = [](const Toast& toast) { return toast.remaining <= 0.0f; };
    m_shown.erase(std::remove_if(m_shown.begin(), m_shown.end(), spent), m_shown.end());
    if (m_shown.empty()) {
        m_hovered = false;
        return std::nullopt;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = EditorStyle::px(12.0f);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + pad, vp->WorkPos.y + vp->WorkSize.y - pad - EditorStatusBar::height()),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f)
    );
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##Toasts", nullptr, flags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImDrawListSplitter layers;
    layers.Split(dl, 2);
    const ImVec2 inner(EditorStyle::px(10.0f), EditorStyle::px(7.0f));
    const ImVec2 between(ImGui::GetStyle().ItemSpacing.x, EditorStyle::px(5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, between);
    size_t dismissed = m_shown.size();
    std::optional<ConsoleView> asked;
    for (size_t i = 0; i < m_shown.size(); ++i) {
        const Toast& toast = m_shown[i];
        // The last moments fade rather than pop out.
        const float alpha = std::min(1.0f, toast.remaining / 0.4f);
        ImVec4 back = TOAST_INFO_BG;
        EditorIcon glyph = EditorIcon::Info;
        if (toast.kind == ToastKind::Error) {
            back  = TOAST_ERROR_BG;
            glyph = EditorIcon::Error;
        } else if (toast.kind == ToastKind::Warning) {
            back  = TOAST_WARNING_BG;
            glyph = EditorIcon::Warning;
        }
        back.w = 0.95f * alpha;

        layers.SetCurrentChannel(dl, 1);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(start.x + inner.x, start.y + inner.y));
        ImGui::BeginGroup();
        inlineIcon(glyph, ImGui::GetTextLineHeight(), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
        ImGui::TextUnformatted(toast.message.c_str());
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        const ImVec2 end(ImGui::GetItemRectMax().x + inner.x, ImGui::GetItemRectMax().y + inner.y);
        ImGui::SetCursorScreenPos(start);
        const std::optional<ConsoleView> opens = toast.opens();
        if (ImGui::InvisibleButton("##toast", ImVec2(end.x - start.x, end.y - start.y))) {
            dismissed = i;
            asked     = opens;
        }
        ImGui::SetItemTooltip("%s", opens ? "Click to see the Console" : "Click to put away");
        ImGui::PopID();
        layers.SetCurrentChannel(dl, 0);
        dl->AddRectFilled(start, end, ImGui::GetColorU32(back), EditorStyle::px(6.0f));
    }
    ImGui::PopStyleVar();
    layers.Merge(dl);
    m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::End();
    if (dismissed < m_shown.size()) m_shown.erase(m_shown.begin() + static_cast<std::ptrdiff_t>(dismissed));
    return asked;
}

} // namespace Vkm::Engine
