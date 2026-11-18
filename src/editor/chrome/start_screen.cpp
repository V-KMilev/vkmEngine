#include "chrome/start_screen.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "editor_context.h"
#include "editor_settings.h"
#include "editor_state.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"
#include "ui/editor_dialogs.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

namespace {

constexpr double RECENTS_LOOKED_AT = 1.0;  ///< Seconds between looks at the recent projects' folders.

using EditorStyle::px;

const ImVec4 PANEL_BG = ImVec4(EditorStyle::GAP.x, EditorStyle::GAP.y, EditorStyle::GAP.z, 0.97f);
    ///< Opaque enough to read on: the sky shows around the panel, not through it.

constexpr ImU32 SCRIM_TOP_U32    = IM_COL32(8, 9, 12, 120);  ///< What the sky is dimmed by, darker towards
                                                             ///< the bottom where the horizon is brightest.
constexpr ImU32 SCRIM_BOTTOM_U32 = IM_COL32(8, 9, 12, 215);

/// One project row: two lines of text and the air around them.
float rowHeight() { return ImGui::GetTextLineHeight() * 2.0f + px(18.0f); }

/**
 * @brief When this project was last open in the editor, in words.
 *
 * Read off editor_settings.json, written as the editor leaves; a project never closed cleanly has none.
 *
 * @param root Project directory.
 * @return A phrase like "3 hours ago", or empty when there is nothing to read.
 */
std::string lastOpened(const fs::path& root) {
    std::error_code ec;
    const auto stamp = fs::last_write_time(EditorSettings::pathIn(root), ec);
    if (ec) return {};

    const auto age     = fs::file_time_type::clock::now() - stamp;
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
 * @brief What a project's project.json says about it.
 *
 * @param root Project directory.
 * @return The project as read; its defaults and its name when the file will not read.
 */
Project describe(const fs::path& root) {
    Project project;
    loadProject(root, project);
    return project;
}

/**
 * @brief A rounded tile with @p name's first letter: a project's avatar, or an example's missing picture.
 *
 * @param dl Draw list to append to.
 * @param min Top-left corner.
 * @param size Its width and height, in pixels.
 * @param name The project's name, whose first letter it shows.
 * @param dim Whether the project is missing.
 */
void drawLetterTile(ImDrawList* dl, ImVec2 min, ImVec2 size, const std::string& name, bool dim) {
    const float alpha  = dim ? 0.45f : 1.0f;
    const ImVec4& fill = EditorStyle::PRESSED;
    const ImVec2  max(min.x + size.x, min.y + size.y);
    dl->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(fill.x, fill.y, fill.z, alpha)), px(6.0f));

    const auto first = std::find_if(name.begin(), name.end(), [](char c) { return std::isalnum(c) != 0; });
    const char letter[2] = {
        first == name.end() ? '?' : static_cast<char>(std::toupper(static_cast<unsigned char>(*first))),
        '\0'
    };
    const float  height = std::min(size.x, size.y) * 0.5f;
    const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(height, FLT_MAX, 0.0f, letter);
    const ImVec4& ink   = EditorStyle::TEXT_SECONDARY;
    dl->AddText(
        ImGui::GetFont(),
        height,
        ImVec2(min.x + (size.x - extent.x) * 0.5f, min.y + (size.y - extent.y) * 0.5f),
        ImGui::GetColorU32(ImVec4(ink.x, ink.y, ink.z, alpha)),
        letter
    );
}

/**
 * @brief A pill holding a short word, right-aligned at @p right.
 *
 * @param dl Draw list to append to.
 * @param right Its right edge.
 * @param centerY The line it sits on.
 * @param text What it says.
 * @param color Its text, and a faint fill of the same.
 * @return Its width.
 */
float drawBadge(ImDrawList* dl, float right, float centerY, const char* text, const ImVec4& color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float padX  = px(7.0f);
    const float h     = size.y + px(4.0f);
    const ImVec2 min(right - size.x - padX * 2.0f, centerY - h * 0.5f);
    dl->AddRectFilled(
        min,
        ImVec2(right, min.y + h),
        ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.16f)),
        h * 0.5f
    );
    dl->AddText(ImVec2(min.x + padX, min.y + px(2.0f)), ImGui::GetColorU32(color), text);
    return size.x + padX * 2.0f;
}

/**
 * @brief One selectable project row: avatar, name and @p detail, and @p note and @p badge at the right.
 *
 * @tparam Menu Callable raising the row's context menu, invoked with no arguments.
 * @param id Unique widget id for the row.
 * @param name Project name, on the first line.
 * @param detail Second line, dim: the path, or what the project is.
 * @param note Right-aligned, dim, or empty for none.
 * @param badge Right-aligned pill, or nullptr for none.
 * @param badgeColor The pill's color.
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
    const ImVec4& badgeColor,
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lineH   = ImGui::GetTextLineHeight();
    const float centerY = (mn.y + mx.y) * 0.5f;

    const float side = rowHeight() - px(14.0f);
    drawLetterTile(dl, ImVec2(mn.x + px(6.0f), centerY - side * 0.5f), ImVec2(side, side), name, dim);

    float right = mx.x - px(10.0f);
    if (badge) right -= drawBadge(dl, right, centerY, badge, badgeColor) + px(10.0f);
    if (!note.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(note.c_str());
        dl->AddText(
            ImVec2(right - size.x, centerY - size.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            note.c_str()
        );
        right -= size.x + px(12.0f);
    }

    const float textX = mn.x + side + px(18.0f);
    const float textW = right - textX;
    const float top   = centerY - lineH;
    const ImVec4& nameColor = ImGui::GetStyleColorVec4(dim ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    dl->AddText(ImVec2(textX, top), ImGui::GetColorU32(nameColor), elidedLine(name.c_str(), textW).c_str());
    dl->AddText(
        ImVec2(textX, top + lineH),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        elidedLine(detail.c_str(), textW).c_str()
    );
    return clicked;
}

} // namespace

void StartScreen::syncRecent(const std::vector<std::string>& roots) {
    if (roots == m_recentRoots) return;

    m_recentRoots = roots;
    m_seenAt      = -1.0;
    m_recent.clear();
    m_recent.reserve(roots.size());
    for (const std::string& root : roots) {
        const Project project = describe(root);
        m_recent.push_back(
            {root, pathKey(root), project.name, project.description, {}, project.engineVersion, false, {}}
        );
    }
}

void StartScreen::scanExamples() {
    if (m_examplesScanned) return;
    m_examplesScanned = true;

    std::error_code ec;
    const fs::path dir = Roots::engineRoot() / "examples";
    if (!fs::is_directory(dir, ec)) return;

    // Stepped by hand: a range-for's error_code covers only the constructor, and a step throws.
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_directory(entryEc)) continue;
        if (!isProjectRoot(it->path())) continue;
        const Project project = describe(it->path());
        const fs::path root   = it->path();
        std::string picture;
        if (!project.picture.empty()) picture = (root / project.picture).string();
        m_examples.push_back({
            it->path().string(),
            pathKey(it->path()),
            project.name,
            project.description,
            picture,
            project.engineVersion,
            false,
            {}
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
    // Looked at again now and then, not each frame: a folder moved or made shows within a second.
    if (ImGui::GetTime() - m_seenAt >= RECENTS_LOOKED_AT || m_seenAt < 0.0) {
        m_seenAt = ImGui::GetTime();
        for (Entry& entry : m_recent) {
            entry.missing = !isProjectRoot(entry.root);
            entry.opened  = entry.missing ? std::string{} : lastOpened(entry.root);
        }
    }
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
            // With nothing of one's own yet, the examples are where to start.
            const bool lead = m_recent.empty() && !m_examplesLed;
            m_examplesLed  |= lead;
            const ImGuiTabItemFlags select = lead ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(label, nullptr, select)) {
                drawExamples(ec);
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
    const float top    = ImGui::GetCursorPosY();
    const float side   = ImGui::GetTextLineHeight() * 2.6f;

    if (drawEngineMark(ec.hooks, side)) ImGui::SameLine(0.0f, px(14.0f));

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
    if (ImGui::Button("Open...", button)) state.requests.ask(Ask::OpenProject);
    ImGui::SameLine(left + button.x + gap);
    ImGui::SetCursorPosY(y);
    ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorStyle::ACCENT_HOV);
    if (ImGui::Button("New Project", button)) {
        state.requests.ask(NewProject{});
    }
    ImGui::PopStyleColor(2);
    ImGui::SetCursorPosY(top + side);
}

void StartScreen::drawProjects(EditorState& state) {
    ImGui::Spacing();

    if (m_recent.empty()) {
        ImGui::TextDisabled("No projects yet: New Project makes one, or start from an example.");
        return;
    }

    searchField("##filter", m_filter, sizeof(m_filter), -FLT_MIN, "Search by name or folder");
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
    int shown = 0;
    for (const bool wantMissing : {false, true}) {
        for (const Entry& entry : m_recent) {
            const bool missing = entry.missing;
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
                        m_moving   = entry;
                        m_moveOpen = true;
                    } else {
                        state.sceneActions.request(SceneActions::Kind::OpenProject, entry.root);
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
            ImVec4 badgeColor = EditorStyle::WARNING;
            if (missing) {
                badge      = "Not found";
                badgeColor = EditorStyle::DANGER;
            } else if (movable) {
                badge = "Made for " + entry.engineVersion;
            }

            ImGui::PushID(entry.root.c_str());
            const bool clicked = projectRow(
                "##recent",
                entry.name,
                entry.root,
                entry.opened,
                badge.empty() ? nullptr : badge.c_str(),
                badgeColor,
                missing,
                menu
            );
            ImGui::PopID();

            if (!clicked || missing) continue;
            if (movable) {
                m_moving   = entry;
                m_moveOpen = true;
            } else {
                state.sceneActions.request(SceneActions::Kind::OpenProject, entry.root);
            }
        }
    }
    if (shown == 0) ImGui::TextDisabled("No project matches \"%s\".", m_filter);
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
}

void StartScreen::drawExamples(EditorContext& ec) {
    EditorState& state = ec.state;
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

    // A 16:9 picture across each card's top, then its name, what it is and the way in.
    const float width   = ImGui::GetContentRegionAvail().x;
    const float gap     = px(12.0f);
    const int columns   = width >= px(780.0f) ? 3 : 2;
    const float across  = static_cast<float>(columns);
    const float cardW   = (width - gap * (across - 1.0f)) / across;
    const float pad     = px(14.0f);
    const float picture = std::floor(cardW * 9.0f / 16.0f);
    const ImVec2 card(cardW, picture + px(140.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorStyle::RAISED);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    for (size_t i = 0; i < m_examples.size(); ++i) {
        const Entry& entry = m_examples[i];
        if (i % static_cast<size_t>(columns) != 0) ImGui::SameLine(0.0f, gap);

        ImGui::PushID(entry.root.c_str());
        if (ImGui::BeginChild("##card", card, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar)) {
            ImDrawList*  dl  = ImGui::GetWindowDrawList();
            const ImVec2 top = ImGui::GetWindowPos();
            const ImVec2 end(top.x + cardW, top.y + picture);
            const GpuTextureId shown = entry.picture.empty() ? 0 : ec.hooks.chromeImage(entry.picture);
            if (shown) {
                // The UVs flip because the decode is bottom-up and ImGui's are not.
                const ImU32 white = IM_COL32_WHITE;
                const float round = px(8.0f);
                const ImDrawFlags corners = ImDrawFlags_RoundCornersTop;
                const ImVec2 uvTop(0.0f, 1.0f);
                const ImVec2 uvEnd(1.0f, 0.0f);
                dl->AddImageRounded(imTexture(shown), top, end, uvTop, uvEnd, white, round, corners);
            } else {
                drawLetterTile(dl, top, ImVec2(cardW, picture), entry.name, false);
            }
            ImGui::SetCursorPosY(picture + pad);
            sectionLabel(entry.name.c_str());
            ImGui::PushTextWrapPos(0.0f);
            const std::string& about = entry.description.empty() ? entry.root : entry.description;
            ImGui::TextDisabled("%s", about.c_str());
            ImGui::PopTextWrapPos();

            // Tinted, as the card's color would swallow a plain button.
            const float   buttonH = px(28.0f);
            const ImVec4& accent  = EditorStyle::ACCENT;
            ImGui::SetCursorPos(ImVec2(pad, card.y - buttonH - pad));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.45f));
            if (ImGui::Button("New Project from This", ImVec2(cardW - pad * 2.0f, buttonH))) {
                state.requests.ask(NewProject{entry.root});
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
        if (saveProjectEngine(m_moving.root)) {
            m_recentRoots.clear();   // Re-read, so the row loses its badge.
            state.sceneActions.request(SceneActions::Kind::OpenProject, m_moving.root);
        } else {
            state.toasts.push(ToastKind::Error, "Could not write " + m_moving.root + "/project.json");
        }
    }
    endDialog();
}

} // namespace Vkm::Engine
