#include "chrome/editor_status_bar.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <imgui.h>
#include <glm/glm.hpp>

#include "core/clock.h"
#include "core/frame_context.h"
#include "debug/error/engine_error_log.h"
#include "panels/console/console_panel.h"
#include "session/play_session.h"
#include "ui/style/editor_icons.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"
#include "editor_state.h"
#include "ui/style/editor_style.h"
#include "editor_context.h"
#include "ui/editor_widgets.h"
#include "ui/entity_labels.h"

namespace Vkm::Engine::EditorStatusBar {

float height() {
    return ImGui::GetFrameHeight() + EditorStyle::px(4.0f);
}

namespace {

/**
 * @brief One of the bar's notices: a glyph and a word in a color, which a click answers.
 *
 * @param id    ImGui id.
 * @param glyph What kind of notice.
 * @param text  What it says.
 * @param color Its tint.
 * @param tip   Shown on hover.
 * @return Whether it was clicked.
 */
bool notice(const char* id, EditorIcon glyph, const char* text, const ImVec4& color, const char* tip) {
    const float side = ImGui::GetTextLineHeight();
    const ImVec2 size(side + EditorStyle::px(4.0f) + ImGui::CalcTextSize(text).x, ImGui::GetFrameHeight());
    const ImVec2 at  = ImGui::GetCursorScreenPos();
    const bool hit   = ImGui::InvisibleButton(id, size);
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    const ImU32 ink  = ImGui::ColorConvertFloat4ToU32(color);
    const float midY = at.y + size.y * 0.5f;
    drawEditorIcon(dl, glyph, ImVec2(at.x + side * 0.5f, midY), side * 0.45f, ink);
    const ImVec2 words(at.x + side + EditorStyle::px(4.0f), midY - ImGui::GetTextLineHeight() * 0.5f);
    dl->AddText(words, ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text) : ink, text);
    ImGui::SetItemTooltip("%s", tip);
    return hit;
}

/// Bring the Console forward on @p view.
void openConsole(EditorState& state, ConsolePanel& console, ConsoleView view) {
    console.show(view);
    state.requests.ask(Ask::RevealConsole);
}

} // namespace

void draw(EditorContext& ec, ConsolePanel& console) {
    const FrameContext& ctx = ec.frame;
    EditorState& state = ec.state;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(EditorStyle::px(8.0f), 0.0f));
    // Same elevation as the menu bar, bookending the workspace.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(EditorStyle::px(8.0f));
        ImGui::AlignTextToFramePadding();

        if (state.selection.active() && ctx.scene.isAlive(state.selection.active())) {
            if (state.selection.entities().size() > 1) {
                ImGui::TextDisabled("%zu selected  |  Active:", state.selection.entities().size());
            } else {
                ImGui::TextDisabled("Selected:");
            }
            ImGui::SameLine(0, EditorStyle::px(4.0f));

            // Up to six levels, so a deep selection reads "Root > Group > Entity".
            char chain[192] = {};
            size_t off = 0;
            EntityId stack[6] = {};
            int count = 0;
            HierarchyOperations::forSelfAndAncestors(ctx.scene, state.selection.active(), [&](EntityId at) {
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
        } else {
            ImGui::TextDisabled("No selection");
        }

        // On the right, what wants a look: a session, whose edits Stop throws away (ejected, the game hears
        // nothing, and nothing else shows it), the problems, each a click from its Console view, then the rate.
        const char* session = !ec.play.isPlaying() ? nullptr
            : ec.play.isEjected() ? "PLAY MODE, EJECTED - the game hears no input; edits are discarded on Stop"
            : "PLAY MODE - edits are discarded on Stop";
        const size_t errors   = ec.errorLog.entries().size();
        const size_t warnings = console.warnings();
        char errorText[32];
        char warningText[32];
        char rateText[32];
        std::snprintf(errorText, sizeof(errorText), "%zu error%s", errors, errors == 1 ? "" : "s");
        std::snprintf(warningText, sizeof(warningText), "%zu warning%s", warnings, warnings == 1 ? "" : "s");
        const float rate = ctx.clock.getFrameRate();
        std::snprintf(rateText, sizeof(rateText), "%.0f FPS", rate);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float glyphW  = ImGui::GetTextLineHeight() + EditorStyle::px(4.0f);
        float wide = ImGui::CalcTextSize(rateText).x;
        if (session)      wide += ImGui::CalcTextSize(session).x + spacing;
        if (errors > 0)   wide += glyphW + ImGui::CalcTextSize(errorText).x + spacing;
        if (warnings > 0) wide += glyphW + ImGui::CalcTextSize(warningText).x + spacing;
        ImGui::SameLine(ImGui::GetWindowWidth() - wide - EditorStyle::px(10.0f));
        if (session) {
            ImGui::TextColored(EditorStyle::WARNING, "%s", session);
            ImGui::SameLine();
        }
        if (errors > 0) {
            const std::string tip = std::string("What the project caused - open the Console's ")
                + consoleViewName(ConsoleView::Errors);
            if (notice("##errors", EditorIcon::Error, errorText, EditorStyle::DANGER, tip.c_str())) {
                openConsole(ec.state, console, ConsoleView::Errors);
            }
            ImGui::SameLine();
        }
        if (warnings > 0) {
            const char* tip = "Warnings this run logged - open the Console";
            if (notice("##warnings", EditorIcon::Warning, warningText, EditorStyle::WARNING, tip)) {
                openConsole(ec.state, console, ConsoleView::Log);
            }
            ImGui::SameLine();
        }
        // Below 30 a frame, the editor itself is in the way of working.
        constexpr float SLOW_RATE = 30.0f;
        const ImVec4 quiet    = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        const ImVec4 rateTone = rate < SLOW_RATE ? EditorStyle::WARNING : quiet;
        ImGui::TextColored(rateTone, "%s", rateText);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace Vkm::Engine::EditorStatusBar
