#include "chrome/editor_status_bar.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/component/core/transform.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "editor_state.h"
#include "ui/editor_style.h"
#include "editor_context.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine::EditorStatusBar {

float height() {
    return ImGui::GetFrameHeight() + EditorStyle::px(4.0f);
}

void draw(EditorContext& ec) {
    const FrameContext& ctx   = ec.frame;
    EditorState&        state = ec.state;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorStyle::px(8.0f), 0.0f));
    // Same elevation as the menu bar, bookending the workspace.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(EditorStyle::px(8.0f));
        ImGui::AlignTextToFramePadding();

        if (state.sceneDirty) {
            const float r = EditorStyle::px(4.0f);
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float centreY = cursor.y + ImGui::GetTextLineHeight() * 0.5f + EditorStyle::px(2.0f);
            const ImVec2 centre(cursor.x + r, centreY);
            ImGui::GetWindowDrawList()->AddCircleFilled(centre, r, ImGui::GetColorU32(EditorStyle::ACCENT));
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
            ImGui::SameLine(0, EditorStyle::px(4.0f));

            // Parent breadcrumb: walk up the hierarchy chain (max 6 levels) so
            // deep selections show "Root > Group > Entity" instead of just the
            // leaf name.
            char chain[192] = {};
            size_t off = 0;
            EntityId stack[6] = {};
            int count = 0;
            HierarchyOperations::forSelfAndAncestors(ctx.scene, state.selectedEntity, [&](EntityId at) {
                stack[count++] = at;
                return count < 6;
            });
            for (int i = count - 1; i >= 0; --i) {
                char buf[64];
                getEntityDisplayName(ctx.scene, stack[i], buf, sizeof(buf));
                int n = snprintf(chain + off, sizeof(chain) - off, i == count - 1 ? "%s" : " > %s", buf);
                if (n > 0) off += static_cast<size_t>(n);
                if (off >= sizeof(chain) - 4) {
                    strcpy(chain + sizeof(chain) - 4, "...");
                    break;
                }
            }
            ImGui::Text("%s", chain);

            if (const Transform* at = ctx.scene.tryGet<Transform>(state.selectedEntity)) {
                const glm::vec3& pos = at->position;
                ImGui::SameLine(0, EditorStyle::px(16.0f));
                ImGui::TextDisabled("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
            }
        } else {
            ImGui::TextDisabled("No selection");
        }

        char right[128];
        snprintf(
            right,
            sizeof(right),
            "%s v%s | %s | %.8s",
            APP_NAME,
            APP_VERSION,
            APP_BRANCH,
            APP_COMMIT_HASH
        );
        float rw = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - rw - EditorStyle::px(16.0f));
        ImGui::TextDisabled("%s", right);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace Vkm::Engine::EditorStatusBar
