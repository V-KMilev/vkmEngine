#include "panels/start_screen.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "framework/editor_common.h"
#include "framework/editor_context.h"
#include "framework/editor_state.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

namespace {

using EditorStyle::px;

/// Width of the column everything is laid out in.
float columnWidth() { return px(560.0f); }

/// Height of one project row: two lines of text and the air around them.
float rowHeight() { return ImGui::GetTextLineHeight() * 2.0f + px(14.0f); }

/**
 * @brief When this project was last open in the editor, in words.
 *
 * From editor_settings.json, which the editor writes into a project as it
 * leaves - so its timestamp is the last time somebody had this project open,
 * not the last time a file inside it changed. A project that has never been
 * closed cleanly has no such file and gets nothing rather than a guess.
 *
 * @param root Project directory.
 * @return A phrase like "3 hours ago", or empty when there is nothing to read.
 */
std::string lastOpened(const fs::path& root) {
    std::error_code ec;
    const auto stamp = fs::last_write_time(root / "editor_settings.json", ec);
    if (ec) return {};

    const auto age = fs::file_time_type::clock::now() - stamp;
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(age).count();
    if (minutes < 1)   return "just now";
    if (minutes < 60)  return std::to_string(minutes) + (minutes == 1 ? " minute ago" : " minutes ago");

    const auto hours = minutes / 60;
    if (hours < 24)    return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");

    const auto days = hours / 24;
    if (days == 1)     return "yesterday";
    if (days < 30)     return std::to_string(days) + " days ago";

    const auto months = days / 30;
    if (months < 12)   return std::to_string(months) + (months == 1 ? " month ago" : " months ago");
    return std::to_string(days / 365) + "y ago";
}

/**
 * @brief The path resolved to one spelling, for comparing two of them.
 *
 * A project reached through a relative path, a symlink or Windows separators is
 * the same project, and the recents list and the examples scan arrive at it by
 * different routes.
 *
 * @param root Path as it was stored or found.
 * @return A comparable form, or the input where the path cannot be resolved.
 */
std::string pathKey(const fs::path& root) {
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(root, ec);
    return ec ? root.lexically_normal().string() : resolved.string();
}

/// The name a project calls itself, or its directory's when the file will not read.
std::string projectName(const fs::path& root) {
    Project project;
    if (loadProject(root, project) && !project.name.empty()) return project.name;

    // A path stored with a trailing separator has an empty filename, and a row
    // headed "(unnamed)" says less than the path already on the line below it.
    const std::string leaf = root.lexically_normal().filename().string();
    return leaf.empty() ? root.string() : leaf;
}

/**
 * @brief One selectable project row: name, path, and when it was last open.
 *
 * An empty selectable with the text drawn over it, because the two lines carry
 * different colours and a row that is gone says so in a third - which one label
 * cannot do.
 *
 * The tooltip and @p menu are raised here rather than by the caller: the lines
 * drawn afterwards are items of their own, and everything ImGui answers about
 * "the item" - hovered, right-clicked - would then be answering about the path
 * line instead of the row.
 *
 * @tparam Menu Callable raising the row's context menu, invoked with no arguments.
 * @param id Unique widget id for the row.
 * @param width Row width; the column's, not the window's, which a zero-width
 *        selectable would stretch to.
 * @param name Project name, drawn on the first line.
 * @param root Absolute path, drawn dim and elided on the second.
 * @param accent Strip colour down the left edge.
 * @param when Right-aligned note, or empty for none.
 * @param missing Whether the path no longer resolves.
 * @param menu Raises the context menu while the row is still the last item.
 * @return Whether the row was clicked.
 */
template <typename Menu>
bool projectRow(const char* id, float width, const std::string& name, const std::string& root,
                const ImVec4& accent, const std::string& when, bool missing, Menu&& menu) {
    const bool clicked = ImGui::Selectable(id, false, ImGuiSelectableFlags_AllowOverlap,
                                           ImVec2(width, rowHeight()));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", root.c_str());
    menu();

    // Both lines go through the draw list rather than the cursor. Text placed
    // with SetCursorScreenPos leaves the cursor somewhere the layout did not put
    // it, and the row after it - or the window - inherits that.
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    ImDrawList*  dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(ImVec2(mn.x, mn.y + px(3.0f)), ImVec2(mn.x + px(3.0f), mx.y - px(3.0f)),
                      ImGui::GetColorU32(missing ? EditorStyle::DANGER : accent), px(1.5f));

    const float textX = mn.x + px(14.0f);
    const float noteW = when.empty() ? 0.0f : ImGui::CalcTextSize(when.c_str()).x + px(12.0f);
    const float textW = mx.x - textX - noteW;
    const float lineH = ImGui::GetTextLineHeight();

    const std::string first  = elidedLine(name.c_str(), textW);
    const std::string second = elidedLine(missing ? (root + "   (not found)").c_str()
                                                  : root.c_str(), textW);
    dl->AddText(ImVec2(textX, mn.y + px(5.0f)),
                ImGui::GetColorU32(missing ? EditorStyle::DANGER
                                           : ImGui::GetStyleColorVec4(ImGuiCol_Text)),
                first.c_str());
    dl->AddText(ImVec2(textX, mn.y + px(5.0f) + lineH),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), second.c_str());

    if (!when.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(when.c_str());
        dl->AddText(ImVec2(mx.x - size.x - px(10.0f), mn.y + (rowHeight() - size.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), when.c_str());
    }
    return clicked;
}

} // namespace

void StartScreen::syncRecent(const std::vector<std::string>& roots) {
    if (roots == m_recentRoots) return;

    m_recentRoots = roots;
    m_recent.clear();
    m_recent.reserve(roots.size());
    for (const std::string& root : roots) {
        m_recent.push_back({root, projectName(root), pathKey(root)});
    }
}

void StartScreen::scanExamples() {
    if (m_examplesScanned) return;
    m_examplesScanned = true;

    // A packaged SDK ships no examples, which is why this is a directory that
    // may not be there rather than a list somebody maintains.
    std::error_code ec;
    const fs::path dir = ProjectPaths::engineRoot() / "examples";
    if (!fs::is_directory(dir, ec)) return;

    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_directory() || !fs::exists(entry.path() / "project.json", ec)) continue;
        m_examples.push_back({entry.path().string(), projectName(entry.path()),
                              pathKey(entry.path())});
    }
    std::sort(m_examples.begin(), m_examples.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });
}

void StartScreen::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    syncRecent(state.recentProjects);
    scanExamples();

    // In a child of its own, because the root window has no decoration and so
    // no scrollbar: eight recents under three examples on a short window is a
    // list whose last rows there would be no way to reach.
    if (!ImGui::BeginChild("##start", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                           ImGuiWindowFlags_NoBackground)) {
        ImGui::EndChild();
        return;
    }

    // Narrower than the column when the window is, and never narrower than a
    // path is worth reading: a negative width is a row that draws inside out.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  width = std::max(px(240.0f), std::min(columnWidth(), avail.x - px(32.0f)));

    ImGui::SetCursorPos(ImVec2((avail.x - width) * 0.5f,
                               std::max(px(40.0f), avail.y * 0.12f)));
    ImGui::BeginGroup();

    // The engine's own mark, through the render seam rather than a texture of
    // the editor's own; the UVs flip because the decode is bottom-up.
    if (EditorRenderHooks* hooks = editorRenderHooks(ec.renderSystem.backend())) {
        const GpuTextureId mark = hooks->chromeImage(
            (ProjectPaths::engineAssets() / "logo" / "vkm_engine_mark.png").string());
        if (mark) {
            const float side = ImGui::GetTextLineHeight() * 2.6f;
            ImGui::Image(imTexture(mark), ImVec2(side, side), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            ImGui::SameLine(0.0f, px(12.0f));
        }
    }

    ImGui::BeginGroup();
    sectionLabel("vkmEngine");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", APP_VERSION);
    ImGui::TextDisabled("A project is a directory with a project.json in it.");
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Spacing();

    const ImVec2 button(width * 0.5f - px(3.0f), px(30.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    if (ImGui::Button("New Project", button)) state.requestNewProject = true;
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0.0f, px(6.0f));
    if (ImGui::Button("Open Project...", button)) state.requestOpenProject = true;

    if (!m_recent.empty()) {
        ImGui::Spacing();
        ImGui::Spacing();
        sectionLabel("Recent");

        std::error_code ec2;
        for (const Entry& entry : m_recent) {
            const bool missing = !fs::exists(fs::path(entry.root) / "project.json", ec2);

            // A path that has moved is the one an author most wants off the
            // list, so the row that reports it is the row that removes it.
            const auto menu = [&] {
                if (!ImGui::BeginPopupContextItem("##row")) return;
                if (ImGui::MenuItem("Open", nullptr, false, !missing)) {
                    state.requestSceneAction(EditorState::SceneAction::OpenProject, entry.root);
                }
                if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(entry.root.c_str());
                if (ImGui::MenuItem("Remove from Recent")) {
                    auto& list = state.recentProjects;
                    list.erase(std::remove(list.begin(), list.end(), entry.root), list.end());
                }
                ImGui::EndPopup();
            };

            ImGui::PushID(entry.root.c_str());
            if (projectRow("##recent", width, entry.name, entry.root, EditorStyle::Accent::Prefab,
                           missing ? std::string{} : lastOpened(entry.root), missing, menu)) {
                state.requestSceneAction(EditorState::SceneAction::OpenProject, entry.root);
            }
            ImGui::PopID();
        }
    }

    if (!m_examples.empty()) {
        ImGui::Spacing();
        ImGui::Spacing();
        sectionLabel("Examples");
        ImGui::TextDisabled("Shipped with the engine, and openable as they are.");
        ImGui::Spacing();

        for (const Entry& entry : m_examples) {
            // Already above, in a list that says when it was last open: an
            // example opened once is a recent project like any other.
            const bool listed = std::any_of(m_recent.begin(), m_recent.end(),
                [&](const Entry& seen) { return seen.key == entry.key; });
            if (listed) continue;

            ImGui::PushID(entry.root.c_str());
            if (projectRow("##example", width, entry.name, entry.root, EditorStyle::Accent::Mesh,
                           std::string{}, false, [] {})) {
                state.requestSceneAction(EditorState::SceneAction::OpenProject, entry.root);
            }
            ImGui::PopID();
        }
    }

    ImGui::EndGroup();
    ImGui::EndChild();
}

} // namespace Vkm::Engine
