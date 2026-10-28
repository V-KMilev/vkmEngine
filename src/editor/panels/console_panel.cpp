#include "panels/console_panel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <string>
#include <vector>

#include <imgui.h>

#include "debug/engine_error_log.h"
#include "editor_state.h"
#include "project_boot.h"
#include "session/build_controller.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/// How each severity is named in the toolbar.
constexpr const char* SEVERITY_NAMES[] = {"Trace", "Verbose", "Debug", "Info", "Warning", "Error", "Fatal"};
static_assert(std::size(SEVERITY_NAMES) == static_cast<size_t>(LogSeverity::Count));

/// The search box's width, design pixels: room for a word or two, the chips beside it.
constexpr float SEARCH_WIDTH = 220.0f;

ImVec4 colorOf(LogSeverity severity) {
    switch (severity) {
        case LogSeverity::Warning: return EditorStyle::WARNING;
        case LogSeverity::Error:
        case LogSeverity::Fatal:   return EditorStyle::DANGER;
        case LogSeverity::Trace:
        case LogSeverity::Verbose:
        case LogSeverity::Debug:   return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        default:                   return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

/// A row's tint behind a warning or worse, faint enough that the text reads over it.
ImU32 rowTint(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.08f));
}

/**
 * @brief Move the cursor so @p width of controls end at the row's right edge.
 *
 * @param width Their total width, spacing included.
 */
void alignRight(float width) {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    ImGui::SameLine(std::max(right, ImGui::GetCursorPosX()));
}

/// The trash button that ends a toolbar.
bool clearButton(const char* tooltip) {
    const float side = ImGui::GetFrameHeight();
    alignRight(side);
    return iconButton("clear", EditorIcon::Trash, false, true, tooltip, side);
}

std::string clockOf(std::chrono::system_clock::time_point at) {
    const std::time_t when = std::chrono::system_clock::to_time_t(at);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &when);
#else
    localtime_r(&when, &tm);
#endif
    char clock[16];
    std::strftime(clock, sizeof(clock), "%H:%M:%S", &tm);
    return clock;
}

} // namespace

ConsolePanel::ConsolePanel() {
    m_shown.fill(true);
    m_shown[static_cast<size_t>(LogSeverity::Trace)]   = false;
    m_shown[static_cast<size_t>(LogSeverity::Verbose)] = false;
    m_tail.open(hostLog().path, hostLog().start);
}

void ConsolePanel::draw(EditorState& state, EngineErrorLog& errors, BuildController& build) {
    const size_t errorCount = errors.entries().size();
    char errorsLabel[32];
    const char* errorsName = consoleViewName(ConsoleView::Errors);
    std::snprintf(errorsLabel, sizeof(errorsLabel), "%s %zu", errorsName, errorCount);
    const Segment views[] = {
        {consoleViewName(ConsoleView::Log), nullptr, "Everything this run logged"},
        {
            errorsLabel,
            errorCount > 0 ? &EditorStyle::DANGER : nullptr,
            "What the project caused: a hook that throws, an asset that will not resolve"
        },
        {
            consoleViewName(ConsoleView::Build),
            build.failed() ? &EditorStyle::DANGER : nullptr,
            "The vkm command running or last run"
        }
    };
    int chosen = static_cast<int>(m_view);
    segmentedControl("##views", views, static_cast<int>(std::size(views)), &chosen);
    m_view = static_cast<ConsoleView>(chosen);
    ImGui::SameLine();

    m_holdsKeys = false;
    switch (m_view) {
        case ConsoleView::Log:    drawLog(); break;
        case ConsoleView::Errors: drawErrors(errors); break;
        case ConsoleView::Build:  build.draw(state); break;
    }
    if (m_holdsKeys) state.consoleHoldsKeys = true;
}

void ConsolePanel::filterLines() {
    uint32_t levels = 0;
    for (size_t level = 0; level < m_shown.size(); ++level) {
        if (m_shown[level]) levels |= 1u << level;
    }
    const bool same = m_tail.revision() == m_filteredRevision && levels == m_filteredLevels
        && m_filteredSearch == m_search;
    if (same) return;
    m_filteredRevision = m_tail.revision();
    m_filteredLevels   = levels;
    m_filteredSearch   = m_search;

    m_visible.clear();
    const auto& lines = m_tail.lines();
    for (size_t i = 0; i < lines.size(); ++i) {
        const LogLine& line = lines[i];
        if (!m_shown[static_cast<size_t>(line.severity)]) continue;
        if (m_search[0] && !matchesFilter(line.text.c_str(), m_search)
            && !matchesFilter(line.category.c_str(), m_search)) {
            continue;
        }
        m_visible.push_back(i);
    }
}

const LogLine* ConsolePanel::selectedLine() const {
    if (m_selected == NONE || m_selected < m_tail.dropped()) return nullptr;
    const uint64_t at = m_selected - m_tail.dropped();
    return at < m_tail.lines().size() ? &m_tail.lines()[static_cast<size_t>(at)] : nullptr;
}

void ConsolePanel::drawLog() {
    const auto& counts = m_tail.counts();
    const auto  count  = [&](LogSeverity severity) { return counts[static_cast<size_t>(severity)]; };

    searchField("##search", m_search, sizeof(m_search), EditorStyle::px(SEARCH_WIDTH));
    if (ImGui::IsItemHovered() && !m_tail.path().empty()) {
        ImGui::SetTooltip("%s", m_tail.path().string().c_str());
    }
    // Error and Fatal are one chip: a fatal line is an error that ended the run.
    const auto chip = [&](const char* name, LogSeverity severity, size_t shown) {
        ImGui::SameLine();
        const size_t level = static_cast<size_t>(severity);
        char label[48];
        std::snprintf(label, sizeof(label), "%s %zu###level%zu", name, shown, level);
        if (toggleChip(label, &m_shown[level], colorOf(severity)) && severity == LogSeverity::Error) {
            m_shown[static_cast<size_t>(LogSeverity::Fatal)] = m_shown[level];
        }
    };
    const size_t failed = count(LogSeverity::Error) + count(LogSeverity::Fatal);
    chip("Debug", LogSeverity::Debug, count(LogSeverity::Debug));
    chip("Info", LogSeverity::Info, count(LogSeverity::Info));
    chip("Warnings", LogSeverity::Warning, count(LogSeverity::Warning));
    chip("Errors", LogSeverity::Error, failed);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Levels").x + ImGui::GetFrameHeight() * 1.5f);
    if (beginCombo("##levels", "Levels")) {
        for (size_t level = 0; level < static_cast<size_t>(LogSeverity::Debug); ++level) {
            char label[48];
            const char* name = SEVERITY_NAMES[level];
            std::snprintf(label, sizeof(label), "%s %zu###level%zu", name, counts[level], level);
            ImGui::Checkbox(label, &m_shown[level]);
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Trace and verbose lines: the engine's chatter, hidden by default");

    filterLines();
    const float side    = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    alignRight(side * 2.0f + spacing);
    if (iconButton("copyShown", EditorIcon::Copy, false, !m_visible.empty(), "Copy the lines shown", side)) {
        std::string all;
        for (const size_t i : m_visible) all += m_tail.lines()[i].text + '\n';
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    const char* clearTip = "Clear the log here, filtered lines too (the file keeps them)";
    if (iconButton("clear", EditorIcon::Trash, false, true, clearTip, side)) {
        m_tail.clear();
        filterLines();
    }

    if (m_visible.empty()) {
        emptyStateHeading(EditorIcon::Search, "Nothing to show", "No logged line matches the filters.", 0);
        m_followed = false;
        return;
    }

    const LogLine* selected = selectedLine();
    const ImGuiStyle& style = ImGui::GetStyle();
    // Room under the list for the selected line in full: long lines run past the table's edge.
    const float detail = selected
        ? ImGui::GetTextLineHeightWithSpacing() * 3.0f + style.WindowPadding.y * 2.0f + style.ItemSpacing.y
        : 0.0f;

    const ImGuiTableFlags table = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
        | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##lines", 4, table, ImVec2(0.0f, -detail))) return;
    ImGui::TableSetupColumn("##level", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize());
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthFixed);
    // As the last frame left it: scrolled up to read, new lines do not pull the view away.
    const bool atEnd = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

    const auto& lines = m_tail.lines();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const size_t   index  = m_visible[static_cast<size_t>(row)];
            const LogLine& line   = lines[index];
            const uint64_t number = m_tail.dropped() + index;
            const ImVec4   tone   = colorOf(line.severity);
            ImGui::TableNextRow();
            if (line.severity >= LogSeverity::Warning) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowTint(tone));
            }
            // By its number in the run, which keeps as the oldest lines go, not by its row.
            ImGui::PushID(static_cast<int>(number));
            ImGui::TableNextColumn();
            if (line.severity >= LogSeverity::Warning) {
                const bool       warns = line.severity == LogSeverity::Warning;
                const EditorIcon glyph = warns ? EditorIcon::Warning : EditorIcon::Error;
                inlineIcon(glyph, ImGui::GetFontSize(), ImGui::ColorConvertFloat4ToU32(tone));
            }
            ImGui::TableNextColumn();
            // The time of day alone; the date is the session's.
            const char* clock = line.time.size() > 11 ? line.time.c_str() + 11 : line.time.c_str();
            const ImGuiSelectableFlags whole = ImGuiSelectableFlags_SpanAllColumns
                | ImGuiSelectableFlags_AllowOverlap;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(clock, number == m_selected, whole)) m_selected = number;
            ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("##line")) {
                if (ImGui::MenuItem("Copy Line")) ImGui::SetClipboardText(line.text.c_str());
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", line.category.c_str());
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, tone);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }

    // At the end, new lines bring the end into view; scrolled up, the list stays put and counts them.
    if (m_followed && !atEnd) ++m_unseen;
    if ((m_followed && atEnd) || m_toEnd) {
        ImGui::SetScrollHereY(1.0f);
        m_unseen = 0;
    }
    if (atEnd) m_unseen = 0;
    m_followed = false;
    m_toEnd    = false;

    // The keys move and copy the selection while the list has the keyboard.
    const bool focused = ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput;
    ImGui::EndTable();
    if (focused && selected) {
        m_holdsKeys = true;
        const bool up   = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
        const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
        if (up || down) {
            const uint64_t first = m_tail.dropped();
            const auto     at    = std::find(m_visible.begin(), m_visible.end(), m_selected - first);
            if (at != m_visible.end()) {
                if (up && at != m_visible.begin()) m_selected = first + *std::prev(at);
                if (down && std::next(at) != m_visible.end()) m_selected = first + *std::next(at);
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            ImGui::SetClipboardText(selected->text.c_str());
        }
    }

    // Over the list's foot: how many arrived while it was scrolled up, and the way down to them.
    if (m_unseen > 0) {
        const ImVec2 corner = ImGui::GetItemRectMax();
        char label[48];
        std::snprintf(label, sizeof(label), "%zu new###toEnd", m_unseen);
        const ImVec2 size(ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f, side);
        const ImVec2 back = ImGui::GetCursorScreenPos();
        const float  inset = style.ScrollbarSize + spacing;
        ImGui::SetCursorScreenPos(ImVec2(corner.x - size.x - inset, corner.y - size.y - spacing));
        ImGui::PushStyleColor(ImGuiCol_Button, EditorStyle::ACCENT);
        if (ImGui::Button(label, size)) m_toEnd = true;
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(back);
    }

    if (selected) {
        if (ImGui::BeginChild("##detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            ImGui::TextDisabled("%s  %s", selected->time.c_str(), selected->category.c_str());
            alignRight(side);
            if (iconButton("copyLine", EditorIcon::Copy, false, true, "Copy this line (Ctrl+C)", side)) {
                ImGui::SetClipboardText(selected->text.c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_Text, colorOf(selected->severity));
            ImGui::TextWrapped("%s", selected->text.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }
}

void ConsolePanel::drawErrors(EngineErrorLog& errors) {
    searchField("##search", m_search, sizeof(m_search), EditorStyle::px(SEARCH_WIDTH));
    if (clearButton("Forget the errors listed here")) errors.clearAll();

    const auto& entries = errors.entries();
    if (entries.empty()) {
        const char* detail = "A failure the project causes - a hook that throws, an asset that will not "
            "resolve - is listed here, newest first.";
        emptyStateHeading(EditorIcon::Check, "No errors", detail, 0);
        return;
    }

    const ImGuiTableFlags table = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("##errors", 4, table)) return;
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed);
    bool any = false;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const EngineErrorLog::Entry& e = *it;
        if (m_search[0] && !matchesFilter(e.message.c_str(), m_search)
            && !matchesFilter(e.source.c_str(), m_search) && !matchesFilter(e.category.c_str(), m_search)) {
            continue;
        }
        any = true;
        ImGui::TableNextRow();
        // By its number, not its place, which the oldest leaving shifts: a click lands where it began.
        ImGui::PushID(static_cast<int>(e.number));
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", clockOf(e.timestamp).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", e.category.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, EditorStyle::DANGER);
        ImGui::TextUnformatted(e.source.c_str());
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", e.message.c_str());
        if (ImGui::BeginPopupContextItem("##error")) {
            if (ImGui::MenuItem("Copy")) ImGui::SetClipboardText((e.source + ": " + e.message).c_str());
            ImGui::EndPopup();
        }
        ImGui::TableNextColumn();
        if (e.repeatCount > 1) ImGui::TextDisabled("x%u", e.repeatCount);
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (!any) ImGui::TextDisabled("No error matches the search.");
}

} // namespace Vkm::Engine
