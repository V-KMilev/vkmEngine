#include "panels/start_screen.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "editor_context.h"
#include "editor_state.h"
#include "io/json_file.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

namespace {

using EditorStyle::px;

/// Opaque enough to read on: the sky shows around the panel, not through it.
const ImVec4 PANEL_BG = ImVec4(0.095f, 0.100f, 0.120f, 0.97f);

/// What the sky is dimmed by, darker towards the bottom where the horizon is brightest.
constexpr ImU32 SCRIM_TOP_U32    = IM_COL32(8, 9, 12, 120);
constexpr ImU32 SCRIM_BOTTOM_U32 = IM_COL32(8, 9, 12, 215);

/// A row's avatar is coloured by the project's name, so a list scans by colour as well as by word.
const ImVec4* const AVATAR_COLOURS[] = {
    &EditorStyle::Accent::PREFAB,
    &EditorStyle::Accent::MESH,
    &EditorStyle::Accent::LIGHT,
    &EditorStyle::Accent::CAMERA,
    &EditorStyle::Accent::ANIM,
    &EditorStyle::Accent::SCRIPT,
    &EditorStyle::Accent::UI,
    &EditorStyle::Accent::ENV
};

/// One project row: two lines of text and the air around them.
float rowHeight() { return ImGui::GetTextLineHeight() * 2.0f + px(18.0f); }

/**
 * @brief When this project was last open in the editor, in words.
 *
 * The timestamp of editor_settings.json, written as the editor leaves; a project never
 * closed cleanly has none.
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

    // Bounded on days: 360 days is twelve 30-day months, which a month bound calls "0 years ago".
    if (days < 365) {
        const auto months = days / 30;
        return std::to_string(months) + (months == 1 ? " month ago" : " months ago");
    }
    const auto years = days / 365;
    return std::to_string(years) + (years == 1 ? " year ago" : " years ago");
}

/**
 * @brief The path resolved to one spelling, for comparing two of them.
 *
 * Relative paths, symlinks and Windows separators reach one project by different routes.
 *
 * @param root Path as stored or found.
 * @return A comparable form, or the input where the path cannot be resolved.
 */
std::string pathKey(const fs::path& root) {
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(root, ec);
    return ec ? root.lexically_normal().string() : resolved.string();
}

/**
 * @brief What a project's project.json says about it, named after its directory when it says nothing.
 *
 * @param root Project directory.
 * @return The project as read; only its name is filled when the file will not read.
 */
Project describe(const fs::path& root) {
    Project project;
    if (loadProject(root, project) && !project.name.empty()) return project;

    // A trailing separator leaves an empty filename; "(unnamed)" would say less than the path.
    const std::string leaf = root.lexically_normal().filename().string();
    project.name = leaf.empty() ? root.string() : leaf;
    return project;
}

/**
 * @brief A rounded square holding the name's first letter.
 *
 * @param dl Draw list to append to.
 * @param min Top-left corner.
 * @param side Side length in pixels.
 * @param name The project's name, which picks the colour and the letter.
 * @param dim Whether the project is missing.
 */
void drawAvatar(ImDrawList* dl, ImVec2 min, float side, const std::string& name, bool dim) {
    const size_t pick = std::hash<std::string>{}(name) % std::size(AVATAR_COLOURS);
    const ImVec4& colour = *AVATAR_COLOURS[pick];
    dl->AddRectFilled(
        min,
        ImVec2(min.x + side, min.y + side),
        ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, dim ? 0.10f : 0.24f)),
        px(6.0f)
    );

    const auto first = std::find_if(name.begin(), name.end(), [](char c) { return std::isalnum(c) != 0; });
    const char letter[2] = {
        first == name.end() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(*first))),
        '\0'
    };
    const float size = ImGui::GetFontSize() * 1.25f;
    const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, letter);
    dl->AddText(
        ImGui::GetFont(),
        size,
        ImVec2(min.x + (side - extent.x) * 0.5f, min.y + (side - extent.y) * 0.5f),
        ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, dim ? 0.45f : 1.0f)),
        letter
    );
}

/**
 * @brief A pill holding a short word, right-aligned at @p right.
 *
 * @param dl Draw list to append to.
 * @param right Its right edge.
 * @param centreY The line it sits on.
 * @param text What it says.
 * @param colour Its text, and a faint fill of the same.
 * @return Its width.
 */
float drawBadge(ImDrawList* dl, float right, float centreY, const char* text, const ImVec4& colour) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float padX = px(7.0f);
    const float h = size.y + px(4.0f);
    const ImVec2 min(right - size.x - padX * 2.0f, centreY - h * 0.5f);
    dl->AddRectFilled(
        min,
        ImVec2(right, min.y + h),
        ImGui::GetColorU32(ImVec4(colour.x, colour.y, colour.z, 0.16f)),
        h * 0.5f
    );
    dl->AddText(ImVec2(min.x + padX, min.y + px(2.0f)), ImGui::GetColorU32(colour), text);
    return size.x + padX * 2.0f;
}

/**
 * @brief One selectable project row: avatar, name and path, and a note and badge at the right.
 *
 * An empty selectable with text drawn over it. The tooltip and @p menu are raised here,
 * while the row is still ImGui's last item.
 *
 * @tparam Menu Callable raising the row's context menu, invoked with no arguments.
 * @param id Unique widget id for the row.
 * @param name Project name, on the first line.
 * @param detail Second line, dim: the path, or what the project is.
 * @param note Right-aligned, dim, or empty for none.
 * @param badge Right-aligned pill, or nullptr for none.
 * @param badgeColour The pill's colour.
 * @param dim Whether the whole row is drawn faint, for a project that is gone.
 * @param menu Raises the context menu while the row is still the last item.
 * @return Whether the row was clicked.
 */
template <typename Menu>
bool projectRow(
    const char* id,
    const std::string& name,
    const std::string& detail,
    const std::string& note,
    const char* badge,
    const ImVec4& badgeColour,
    bool dim,
    Menu&& menu
) {
    const bool clicked = ImGui::Selectable(
        id,
        false,
        ImGuiSelectableFlags_AllowOverlap,
        ImVec2(0.0f, rowHeight())
    );
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", detail.c_str());
    menu();

    // Draw list, not the cursor: SetCursorScreenPos would leave the layout misplaced.
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const float  lineH   = ImGui::GetTextLineHeight();
    const float  centreY = (mn.y + mx.y) * 0.5f;

    const float side = rowHeight() - px(14.0f);
    drawAvatar(dl, ImVec2(mn.x + px(6.0f), centreY - side * 0.5f), side, name, dim);

    float right = mx.x - px(10.0f);
    if (badge) right -= drawBadge(dl, right, centreY, badge, badgeColour) + px(10.0f);
    if (!note.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(note.c_str());
        dl->AddText(
            ImVec2(right - size.x, centreY - size.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            note.c_str()
        );
        right -= size.x + px(12.0f);
    }

    const float textX = mn.x + side + px(18.0f);
    const float textW = right - textX;
    const float top   = centreY - lineH;
    const ImVec4& nameColour = ImGui::GetStyleColorVec4(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    dl->AddText(ImVec2(textX, top), ImGui::GetColorU32(nameColour), elidedLine(name.c_str(), textW).c_str());
    dl->AddText(
        ImVec2(textX, top + lineH),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        elidedLine(detail.c_str(), textW).c_str()
    );
    return clicked;
}

/**
 * @brief Stamp a project for this engine, touching no other key of its project.json.
 *
 * @param root Project directory.
 * @return Whether the file was rewritten.
 */
bool moveToThisEngine(const fs::path& root) {
    const fs::path file = root / "project.json";
    nlohmann::json doc;
    if (!detail::readJsonFile(file, doc, "project") || !doc.is_object()) return false;
    doc["engineVersion"] = APP_VERSION;
    return detail::writeJsonFile(file, doc, "project");
}

} // namespace

void StartScreen::syncRecent(const std::vector<std::string>& roots) {
    if (roots == m_recentRoots) return;

    m_recentRoots = roots;
    m_recent.clear();
    m_recent.reserve(roots.size());
    for (const std::string& root : roots) {
        const Project project = describe(root);
        m_recent.push_back({root, pathKey(root), project.name, project.description, project.engineVersion});
    }
}

void StartScreen::scanExamples() {
    if (m_examplesScanned) return;
    m_examplesScanned = true;

    std::error_code ec;
    const fs::path dir = ProjectPaths::engineRoot() / "examples";
    if (!fs::is_directory(dir, ec)) return;

    // Stepped by hand: a range-for's error_code covers only the constructor, and a step throws.
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_directory(entryEc)) continue;
        if (!fs::exists(it->path() / "project.json", entryEc)) continue;
        const Project project = describe(it->path());
        m_examples.push_back({
            it->path().string(),
            pathKey(it->path()),
            project.name,
            project.description,
            project.engineVersion
        });
    }
    std::sort(
        m_examples.begin(),
        m_examples.end(),
        [](const Entry& a, const Entry& b) { return a.name < b.name; }
    );
}

void StartScreen::draw(EditorContext& ec) {
    EditorState& state = ec.state;

    syncRecent(state.recentProjects);
    scanExamples();

    // The sky stays, dimmed under the panel so the text reads.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(
        viewport->Pos,
        ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
        SCRIM_TOP_U32,
        SCRIM_TOP_U32,
        SCRIM_BOTTOM_U32,
        SCRIM_BOTTOM_U32
    );

    // A child, because the undecorated root window has no scrollbar.
    const bool open = ImGui::BeginChild(
        "##start",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground
    );
    if (!open) {
        ImGui::EndChild();
        return;
    }

    // Shrinks with the window, never past it.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 panel(
        std::min(avail.x - px(16.0f), std::max(px(560.0f), std::min(px(920.0f), avail.x - px(64.0f)))),
        std::min(avail.y - px(16.0f), std::max(px(380.0f), std::min(px(680.0f), avail.y - px(96.0f))))
    );
    ImGui::SetCursorPos(ImVec2((avail.x - panel.x) * 0.5f, (avail.y - panel.y) * 0.45f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, PANEL_BG);
    ImGui::PushStyleColor(ImGuiCol_Border, EditorStyle::OVERLAY_EDGE);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28.0f), px(24.0f)));
    const bool hub = ImGui::BeginChild(
        "##hub",
        panel,
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar
    );
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    if (hub) {
        drawHeader(ec, ImGui::GetContentRegionAvail().x);
        ImGui::Dummy(ImVec2(0.0f, px(14.0f)));

        // Unselected tabs as bare labels on the panel; the selected one keeps its overline.
        // Pushed for the whole bar: each tab is drawn as it begins.
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline)) {
            char label[48];
            std::snprintf(label, sizeof(label), "Projects  %zu###projects", m_recent.size());
            if (ImGui::BeginTabItem(label)) {
                drawProjects(state);
                ImGui::EndTabItem();
            }
            std::snprintf(label, sizeof(label), "Examples  %zu###examples", m_examples.size());
            const ImGuiTabItemFlags select = m_showExamples ? ImGuiTabItemFlags_SetSelected : 0;
            m_showExamples = false;
            if (ImGui::BeginTabItem(label, nullptr, select)) {
                drawExamples(state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor();
        drawMoveDialog(state);
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

void StartScreen::drawHeader(EditorContext& ec, float width) {
    EditorState& state = ec.state;
    const float top  = ImGui::GetCursorPosY();
    const float side = ImGui::GetTextLineHeight() * 2.6f;

    // The engine's mark, through the render seam; UVs flip as the decode is bottom-up.
    if (EditorRenderHooks* hooks = editorRenderHooks(ec.renderSystem.backend())) {
        const GpuTextureId mark = hooks->chromeImage(
            (ProjectPaths::engineAssets() / "logo" / "vkm_engine_mark.png").string()
        );
        if (mark) {
            ImGui::Image(imTexture(mark), ImVec2(side, side), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            ImGui::SameLine(0.0f, px(14.0f));
        }
    }

    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
    ImGui::TextUnformatted("vkmEngine");
    ImGui::PopFont();
    ImGui::TextDisabled("Version %s", APP_VERSION);
    ImGui::EndGroup();

    // Both placed outright: SameLine alone would sit the second on the first's text line.
    const ImVec2 button(px(132.0f), px(32.0f));
    const float  gap  = px(8.0f);
    const float  left = width - button.x * 2.0f - gap + ImGui::GetStyle().WindowPadding.x;
    const float  y    = top + (side - button.y) * 0.5f;
    ImGui::SameLine(left);
    ImGui::SetCursorPosY(y);
    if (ImGui::Button("Open...", button)) state.requestOpenProject = true;
    ImGui::SameLine(left + button.x + gap);
    ImGui::SetCursorPosY(y);
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    if (ImGui::Button("New Project", button)) {
        state.newProjectTemplate.clear();
        state.requestNewProject = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::SetCursorPosY(top + side);
}

void StartScreen::drawProjects(EditorState& state) {
    ImGui::Spacing();

    if (m_recent.empty()) {
        ImGui::Dummy(ImVec2(0.0f, px(40.0f)));
        const char* headline = "No projects yet";
        const char* detail   = "Make one with New Project, or start from a copy of an example.";
        centreNextItem(ImGui::CalcTextSize(headline).x);
        sectionLabel(headline);
        centreNextItem(ImGui::CalcTextSize(detail).x);
        ImGui::TextDisabled("%s", detail);
        if (!m_examples.empty()) {
            ImGui::Spacing();
            const ImVec2 button(px(160.0f), px(30.0f));
            centreNextItem(button.x);
            if (ImGui::Button("Browse Examples", button)) m_showExamples = true;
        }
        return;
    }

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Search by name or folder", m_filter, sizeof(m_filter));
    ImGui::Spacing();

    const bool listed = ImGui::BeginChild(
        "##list",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground
    );
    if (!listed) {
        ImGui::EndChild();
        return;
    }

    // A neutral lift on hover: the accent would drown a row's badge.
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 1.0f, 1.0f, 0.09f));

    // Found ones first, in the recents' order; the gone ones after, faint, until removed.
    std::error_code ec;
    int shown = 0;
    for (const bool wantMissing : {false, true}) {
        for (const Entry& entry : m_recent) {
            const bool missing = !fs::exists(fs::path(entry.root) / "project.json", ec);
            if (missing != wantMissing) continue;
            const bool matches = matchesFilter(entry.name.c_str(), m_filter)
                || matchesFilter(entry.root.c_str(), m_filter);
            if (!matches) continue;
            ++shown;

            const bool movable = !missing && !compatibleEngine(entry.engineVersion);
            const auto menu = [&] {
                if (!ImGui::BeginPopupContextItem("##row")) return;
                if (ImGui::MenuItem("Open", nullptr, false, !missing)) {
                    if (movable) {
                        m_moving = entry;
                        m_moveOpen = true;
                    } else {
                        state.requestSceneAction(EditorState::SceneAction::OpenProject, entry.root);
                    }
                }
                if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(entry.root.c_str());
                if (ImGui::MenuItem("Remove from List")) {
                    auto& list = state.recentProjects;
                    list.erase(std::remove(list.begin(), list.end(), entry.root), list.end());
                }
                ImGui::EndPopup();
            };

            std::string badge;
            ImVec4 badgeColour = EditorStyle::WARNING;
            if (missing) {
                badge = "Not found";
                badgeColour = EditorStyle::DANGER;
            } else if (movable) {
                badge = "Made for " + entry.engineVersion;
            }

            ImGui::PushID(entry.root.c_str());
            const bool clicked = projectRow(
                "##recent",
                entry.name,
                entry.root,
                missing ? std::string{} : lastOpened(entry.root),
                badge.empty() ? nullptr : badge.c_str(),
                badgeColour,
                missing,
                menu
            );
            ImGui::PopID();

            if (!clicked || missing) continue;
            if (movable) {
                m_moving = entry;
                m_moveOpen = true;
            } else {
                state.requestSceneAction(EditorState::SceneAction::OpenProject, entry.root);
            }
        }
    }
    if (shown == 0) ImGui::TextDisabled("No project matches \"%s\".", m_filter);
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
}

void StartScreen::drawExamples(EditorState& state) {
    ImGui::Spacing();
    if (m_examples.empty()) {
        ImGui::TextDisabled("This engine ships no examples.");
        return;
    }
    ImGui::TextDisabled("Each makes a new project of yours from a copy; the example stays as shipped.");
    ImGui::Spacing();

    const bool cards = ImGui::BeginChild(
        "##cards",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground
    );
    if (!cards) {
        ImGui::EndChild();
        return;
    }

    const float width   = ImGui::GetContentRegionAvail().x;
    const float gap     = px(12.0f);
    const int   columns = width >= px(780.0f) ? 3 : 2;
    const float  across = static_cast<float>(columns);
    const ImVec2 card((width - gap * (across - 1.0f)) / across, px(176.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::CARD_HEADER);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14.0f), px(14.0f)));
    for (size_t i = 0; i < m_examples.size(); ++i) {
        const Entry& entry = m_examples[i];
        if (i % static_cast<size_t>(columns) != 0) ImGui::SameLine(0.0f, gap);

        ImGui::PushID(entry.root.c_str());
        if (ImGui::BeginChild(
                "##card",
                card,
                ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar
            )) {
            const float side = ImGui::GetTextLineHeight() * 1.8f;
            drawAvatar(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), side, entry.name, false);
            ImGui::Dummy(ImVec2(side, side));
            ImGui::SameLine(0.0f, px(10.0f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (side - ImGui::GetTextLineHeight()) * 0.5f);
            sectionLabel(entry.name.c_str());

            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            const std::string& about = entry.description.empty() ? entry.root : entry.description;
            ImGui::TextDisabled("%s", about.c_str());
            ImGui::PopTextWrapPos();

            // Tinted, as the card's colour would swallow a plain button.
            const float buttonH = px(28.0f);
            const ImVec4& accent = EditorStyle::ACCENT;
            ImGui::SetCursorPosY(card.y - buttonH - px(14.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.45f));
            if (ImGui::Button("New Project from This", ImVec2(-FLT_MIN, buttonH))) {
                state.newProjectTemplate = entry.root;
                state.requestNewProject  = true;
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void StartScreen::drawMoveDialog(EditorState& state) {
    if (!beginDialog("Move Project", m_moveOpen)) return;

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(420.0f));
    ImGui::Text(
        "%s was made for vkmEngine %s, and this is %s.",
        m_moving.name.c_str(),
        m_moving.engineVersion.c_str(),
        APP_VERSION
    );
    ImGui::Spacing();
    ImGui::TextDisabled(
        "A project's code builds only against the release it names. Moving it names this one "
        "in its project.json; its code may then need changes to build."
    );
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    if (dialogButtons(m_moveOpen, "Move and Open") == DialogResult::Confirm) {
        if (moveToThisEngine(m_moving.root)) {
            m_recentRoots.clear();   // Re-read, so the row loses its badge.
            state.requestSceneAction(EditorState::SceneAction::OpenProject, m_moving.root);
        } else {
            state.pushToast(ToastKind::Error, "Could not write " + m_moving.root + "/project.json");
        }
    }
    endDialog();
}

} // namespace Vkm::Engine
