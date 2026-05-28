#include "framework/editor_status_bar.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/system.h"
#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "ui/editor_widgets.h"

namespace Engine {

void EditorStatusBar::draw(EditorContext& ec) {
    const FrameContext& ctx   = ec.frame;
    EditorState&        state = ec.state;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(8);
        ImGui::AlignTextToFramePadding();

        // Dirty indicator on the left edge. A small accent dot is louder than
        // a star and doesn't get lost next to the selection text.
        if (state.sceneDirty) {
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(ImGui::GetCursorScreenPos().x + 4.0f,
                       ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f + 2.0f),
                4.0f, ImGui::GetColorU32(EditorStyle::ACCENT));
            ImGui::Dummy(ImVec2(12.0f, 0.0f));
            ImGui::SameLine(0, 0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unsaved changes");
        }

        if (state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
            ImGui::TextDisabled("Selected:");
            ImGui::SameLine(0, 4);

            // Parent breadcrumb: walk up the hierarchy chain (max 5 levels) so
            // deep selections show "Root > Group > Entity" instead of just the
            // leaf name. Keeps the bar one-line by truncating with ellipses.
            char chain[192] = {};
            size_t off = 0;
            EntityId stack[6] = {};
            int depth = 0;
            EntityId cur = state.selectedEntity;
            for (; cur && depth < 6; ++depth) {
                stack[depth] = cur;
                if (!ctx.scene.has<Hierarchy>(cur)) break;
                EntityId p = ctx.scene.get<Hierarchy>(cur).parent;
                if (!p) break;
                cur = p;
            }
            for (int i = depth - 1; i >= 0; --i) {
                char buf[64];
                getEntityDisplayName(ctx.scene, stack[i], buf, sizeof(buf));
                int n = snprintf(chain + off, sizeof(chain) - off,
                    i == depth - 1 ? "%s" : " > %s", buf);
                if (n > 0) off += static_cast<size_t>(n);
                if (off >= sizeof(chain) - 4) { strcpy(chain + sizeof(chain) - 4, "..."); break; }
            }
            ImGui::Text("%s", chain);

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
