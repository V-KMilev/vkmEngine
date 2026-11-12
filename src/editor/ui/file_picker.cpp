#define VKM_LOG_CATEGORY "EDITOR"

#include "ui/file_picker.h"

#include <algorithm>
#include <cstdio>
#include <system_error>
#include <utility>

#include "vkm_log/logger.h"

#include "import/import_formats.h"

#include "ui/editor_dialogs.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {
bool extMatches(const std::vector<std::string>& exts, const std::filesystem::path& p) {
    if (exts.empty()) return true;
    const std::string e = lowercaseExtension(p);
    for (const std::string& ex : exts) if (e == ex) return true;
    return false;
}

std::string displayName(
    const std::filesystem::path& p,
    const std::filesystem::path& relativeTo,
    bool recursive
) {
    if (!relativeTo.empty()) {
        std::error_code ec;
        const std::string rel = std::filesystem::relative(p, relativeTo, ec).generic_string();
        return ec ? p.string() : rel;
    }
    return recursive ? p.string() : p.filename().string();
}
} // namespace

void FilePicker::open(Options options) {
    m_options  = std::move(options);
    m_wantOpen = true;
    rescan();
}

void FilePicker::rescan() {
    m_entries.clear();
    m_paths.clear();
    m_filter[0] = '\0';
    m_selected.clear();
    m_anchor    = -1;
    m_truncated = false;

    std::error_code ec;
    auto consider = [&](const std::filesystem::directory_entry& e) -> bool {
        if (static_cast<int>(m_paths.size()) >= m_options.maxResults) {
            m_truncated = true;
            return false;
        }
        // The error_code forms: an entry that cannot be stat'd is skipped, not thrown mid-frame.
        std::error_code entryEc;
        const bool isFile = e.is_regular_file(entryEc);
        const bool isDir  = e.is_directory(entryEc);
        if (m_options.kind == Kind::Files && !isFile) return true;
        if (m_options.kind == Kind::Directories && !isDir) return true;
        if (m_options.kind == Kind::Files && !extMatches(m_options.extensions, e.path())) return true;
        m_paths.push_back(e.path());
        m_entries.push_back(displayName(e.path(), m_options.relativeTo, m_options.recursive));
        return true;
    };

    // Stepped by hand: a range-for gives the error code to the constructor alone, so an
    // unreadable subdirectory would throw out of an ImGui frame. A failed step ends the walk.
    namespace fs = std::filesystem;
    if (m_options.recursive) {
        for (fs::recursive_directory_iterator it(m_options.root, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!consider(*it)) break;
        }
    } else {
        for (fs::directory_iterator it(m_options.root, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!consider(*it)) break;
        }
    }

    // Loudly: an empty picker without a warning looks like an empty directory.
    if (ec) {
        LOG_WARNING(
            "FilePicker: cannot iterate %s - %s",
            m_options.root.string().c_str(),
            ec.message().c_str()
        );
    }

    std::vector<size_t> order(m_paths.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return m_entries[a] < m_entries[b]; });
    std::vector<std::string> e2(m_entries.size());
    std::vector<std::filesystem::path> p2(m_paths.size());
    for (size_t i = 0; i < order.size(); ++i) {
        e2[i] = std::move(m_entries[order[i]]);
        p2[i] = std::move(m_paths[order[i]]);
    }
    m_entries = std::move(e2);
    m_paths   = std::move(p2);
}

void FilePicker::click(const std::vector<int>& shown, int clicked) {
    const ImGuiIO& io = ImGui::GetIO();
    const auto from   = std::find(shown.begin(), shown.end(), m_anchor);
    if (m_options.many && io.KeyShift && from != shown.end()) {
        const auto to = std::find(shown.begin(), shown.end(), clicked);
        m_selected.assign(std::min(from, to), std::max(from, to) + 1);
        return;
    }
    const auto held = std::find(m_selected.begin(), m_selected.end(), clicked);
    if (m_options.many && io.KeyCtrl) {
        if (held != m_selected.end()) m_selected.erase(held);
        else m_selected.insert(std::upper_bound(m_selected.begin(), m_selected.end(), clicked), clicked);
    } else {
        m_selected = {clicked};
    }
    m_anchor = clicked;
}

bool FilePicker::draw(std::string& outPath) {
    std::vector<std::string> picks;
    if (!draw(picks)) return false;
    outPath = picks.front();
    return true;
}

bool FilePicker::draw(std::vector<std::string>& outPaths) {
    bool picked = false;
    const ImVec2 initialSize(EditorStyle::px(520.0f), EditorStyle::px(400.0f));
    const ImVec2 minimumSize(EditorStyle::px(380.0f), EditorStyle::px(260.0f));
    if (beginDialog(m_options.title, m_wantOpen, initialSize, minimumSize)) {
        ImGui::TextDisabled("%s", m_options.root.string().c_str());
        if (m_truncated) {
            ImGui::SameLine(0, EditorStyle::px(12.0f));
            ImGui::TextColored(
                EditorStyle::WARNING,
                "first %d only - narrow the search",
                m_options.maxResults
            );
        }
        if (!m_options.hint.empty()) {
            ImGui::SameLine(0, EditorStyle::px(12.0f));
            ImGui::TextDisabled("%s", m_options.hint.c_str());
        }

        // Focused on open so type-to-narrow needs no click.
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        const bool searchCommit = ImGui::InputTextWithHint(
            "##pickerFilter",
            "Search...",
            m_filter,
            sizeof(m_filter),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

        std::vector<int> view;
        view.reserve(m_entries.size());
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (matchesFilter(m_entries[i].c_str(), m_filter)) view.push_back(i);
        }

        // The selection survives a filter change; only what the filter shows is handed over.
        std::vector<int> chosen;
        for (const int i : m_selected) {
            if (matchesFilter(m_entries[i].c_str(), m_filter)) chosen.push_back(i);
        }

        auto confirm = [&](const std::vector<int>& rows) {
            outPaths.clear();
            for (const int i : rows) {
                outPaths.push_back(m_options.relativeTo.empty() ? m_paths[i].string() : m_entries[i]);
            }
            picked = true;
            // Both halves: beginDialog reopens from the flag, so closing the popup alone reopens it.
            m_wantOpen = false;
            ImGui::CloseCurrentPopup();
        };

        const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("##AssetPickerList", ImVec2(0, -footerH), ImGuiChildFlags_Borders)) {
            if (view.empty() && !m_entries.empty()) {
                ImGui::TextDisabled("(nothing matches the search)");
            } else if (view.empty()) {
                // Where the files go, since the list shows only that folder.
                ImGui::TextDisabled("No files of these kinds in %s.", m_options.root.string().c_str());
                ImGui::TextDisabled("Drop them on the editor's window to copy them in, or copy them there.");
            } else {
                const ImGuiSelectableFlags rowFlags = ImGuiSelectableFlags_AllowDoubleClick;
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(view.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const int i    = view[row];
                        const bool sel = std::binary_search(m_selected.begin(), m_selected.end(), i);
                        if (ImGui::Selectable(m_entries[i].c_str(), sel, rowFlags)) {
                            click(view, i);
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) confirm({i});
                        }
                    }
                }
            }
        }
        ImGui::EndChild();

        // Enter from the search field: the selection, or the single match.
        if (!picked && searchCommit) {
            if (!chosen.empty()) {
                confirm(chosen);
            } else if (view.size() == 1) {
                confirm({view[0]});
            }
        }

        if (!picked && dialogButtons(m_wantOpen, "Open", !chosen.empty()) == DialogResult::Confirm) {
            confirm(chosen);
        }
        endDialog();
    }
    return picked;
}

} // namespace Vkm::Engine
