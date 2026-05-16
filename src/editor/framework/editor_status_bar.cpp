#include "framework/editor_status_bar.h"
#include "framework/editor_context.h"
#include "framework/editor_common.h"
#include "ui/editor_widgets.h"

#include <imgui.h>

#include <cstdio>

#include "core/system.h"

namespace Engine {

void EditorStatusBar::draw(EditorContext& ec) {
    const FrameContext& ctx   = ec.frame;
    EditorState&        state = ec.state;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(8);
        ImGui::AlignTextToFramePadding();

        if (state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
            ImGui::TextDisabled("Selected:");
            ImGui::SameLine(0, 4);
            char selName[64];
            getEntityDisplayName(ctx.scene, state.selectedEntity, selName, sizeof(selName));
            ImGui::Text("%s", selName);

            if (ctx.scene.has<Transform>(state.selectedEntity)) {
                const auto& pos = ctx.scene.get<Transform>(state.selectedEntity).position;
                ImGui::SameLine(0, 16);
                ImGui::TextDisabled("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
            }
        } else {
            ImGui::TextDisabled("No selection");
        }

        char right[128];
        snprintf(right, sizeof(right), "%s v%s | %s | %.8s",
                 APP_NAME, APP_VERSION, APP_BRANCH, APP_COMMIT_HASH);
        float rw = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - rw - 16);
        ImGui::TextDisabled("%s", right);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace Engine
