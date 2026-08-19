#include "framework/editor_status_bar.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>

#include "core/system.h"
#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

void EditorStatusBar::draw(EditorContext& ec) {
    const FrameContext& ctx   = ec.frame;
    EditorState&        state = ec.state;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
    // Same elevation as the menu bar, bookending the workspace.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(EditorStyle::px(8.0f));
        ImGui::AlignTextToFramePadding();

        // Dirty indicator on the left edge. A small accent dot is louder than
        // a star and doesn't get lost next to the selection text.
        if (state.sceneDirty) {
            const float r = EditorStyle::px(4.0f);
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(ImGui::GetCursorScreenPos().x + r,
                       ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f + 2.0f),
                r, ImGui::GetColorU32(EditorStyle::ACCENT));
            // Full line height so the dot is actually hoverable for its tooltip.
            ImGui::Dummy(ImVec2(EditorStyle::px(12.0f), ImGui::GetTextLineHeight()));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unsaved changes");
            ImGui::SameLine(0, 0);
        }

        if (state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)) {
            if (state.selection.size() > 1) {
                ImGui::TextDisabled("%zu selected  |  Active:", state.selection.size());
            } else {
                ImGui::TextDisabled("Selected:");
            }
            ImGui::SameLine(0, 4);

            // Parent breadcrumb: walk up the hierarchy chain (max 6 levels) so
            // deep selections show "Root > Group > Entity" instead of just the
            // leaf name. Keeps the bar one-line by truncating with ellipses.
            char chain[192] = {};
            size_t off = 0;
            EntityId stack[6] = {};
            // Count the entry as stored, not as stepped over: the walk leaves
            // through a break as often as through the loop condition.
            int count = 0;
            EntityId cur = state.selectedEntity;
            while (cur && count < 6) {
                stack[count++] = cur;
                if (!ctx.scene.has<Hierarchy>(cur)) break;
                EntityId p = ctx.scene.get<Hierarchy>(cur).parent;
                if (!p) break;
                cur = p;
            }
            for (int i = count - 1; i >= 0; --i) {
                char buf[64];
                getEntityDisplayName(ctx.scene, stack[i], buf, sizeof(buf));
                int n = snprintf(chain + off, sizeof(chain) - off,
                    i == count - 1 ? "%s" : " > %s", buf);
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
        ImGui::SameLine(ImGui::GetWindowWidth() - rw - EditorStyle::px(16.0f));
        ImGui::TextDisabled("%s", right);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace Vkm::Engine
