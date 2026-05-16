#include "playback_bar.h"
#include "../editor_common.h"

namespace Engine {

namespace {
    constexpr float BTN = 26.0f;
    constexpr float GAP = 4.0f;
    constexpr float PAD = 5.0f;
}

void ViewportPlaybar::draw(FrameContext& ctx) {
    size_t total = 0, playing = 0;
    ctx.scene.forEach<Animation>([&](EntityId, const Animation& a) {
        ++total;
        if (a.playing) ++playing;
    });
    const bool any       = total > 0;
    const bool isPlaying = playing > 0;

    const float barH = BTN + PAD * 2.0f + 2.0f;
    const float barW = BTN * 2.0f + GAP + PAD * 2.0f + 2.0f;
    ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - barW) * 0.5f, 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::OVERLAY_BG);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PAD, PAD));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(GAP, 0.0f));

    if (ImGui::BeginChild("##ViewportPlaybar", ImVec2(barW, barH), ImGuiChildFlags_Borders)) {
        if (iconButton("vpPlay", isPlaying ? EditorIcon::Pause : EditorIcon::Play,
                       isPlaying, any,
                       isPlaying ? "Pause all animations" : "Play all animations", BTN)) {
            const bool v = !isPlaying;
            ctx.scene.forEach<Animation>([&](EntityId, Animation& a) { a.playing = v; });
        }
        ImGui::SameLine();
        if (iconButton("vpStop", EditorIcon::Stop, false, any,
                       "Stop -- pause and rewind all animations", BTN)) {
            ctx.scene.forEach<Animation>([&](EntityId, Animation& a) {
                a.playing = false;
                a.time = 0.0f;
            });
        }

        m_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    } else {
        m_hovered = false;
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace Engine
