#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/asset_picker.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cctype>
#include <system_error>

#include "logger.h"

#include "ui/editor_dialogs.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Engine {

namespace {
std::string lowerExt(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool extMatches(const std::vector<std::string>& exts, const std::filesystem::path& p) {
    if (exts.empty()) return true;
    const std::string e = lowerExt(p);
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
}

void AssetPicker::open() {
    m_openRequested = true;
}

void AssetPicker::refreshIfNeeded() {
    if (!m_openRequested) return;
    m_openRequested = false;

    m_entries.clear();
    m_paths.clear();
    m_filter[0] = '\0';
    m_selected  = -1;
    m_truncated = false;

    std::error_code ec;
    auto consider = [&](const std::filesystem::directory_entry& e) -> bool {
        if (static_cast<int>(m_paths.size()) >= options.maxResults) {
            m_truncated = true;
            return false;
        }
        const bool isFile = e.is_regular_file();
        const bool isDir  = e.is_directory();
        if (options.kind == Kind::Files && !isFile) return true;
        if (options.kind == Kind::Directories && !isDir) return true;
        if (options.kind == Kind::Files && !extMatches(options.extensions, e.path())) return true;
        m_paths.push_back(e.path());
        m_entries.push_back(displayName(e.path(), options.relativeTo, options.recursive));
        return true;
    };

    if (options.recursive) {
        for (const auto& e :
                std::filesystem::recursive_directory_iterator(options.root, ec)) {
            if (!consider(e)) break;
        }
    } else {
        for (const auto& e : std::filesystem::directory_iterator(options.root, ec)) {
            if (!consider(e)) break;
        }
    }

    // Surface unreadable roots loudly - an empty picker with no warning
    // looks like "no results", which is indistinguishable from a real
    // empty directory.
    if (ec) {
        LOG_WARNING("AssetPicker: cannot iterate %s - %s",
            options.root.string().c_str(), ec.message().c_str());
    }

    // Sort by display name for a stable, predictable browse experience.
    std::vector<size_t> order(m_paths.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
        [&](size_t a, size_t b) { return m_entries[a] < m_entries[b]; });
    std::vector<std::string> e2(m_entries.size());
    std::vector<std::filesystem::path> p2(m_paths.size());
    for (size_t i = 0; i < order.size(); ++i) {
        e2[i] = std::move(m_entries[order[i]]);
        p2[i] = std::move(m_paths[order[i]]);
    }
    m_entries = std::move(e2);
    m_paths   = std::move(p2);

    char titleId[160];
    snprintf(titleId, sizeof(titleId), "%s###%s", options.title, options.popupId);
    ImGui::OpenPopup(titleId);
}

bool AssetPicker::draw(std::string& outPath) {
    refreshIfNeeded();

    // Real title in the bar, stable id after ### (the raw popupId used to BE
    // the visible title, so dialogs were named "PickEnvHdr").
    char titleId[160];
    snprintf(titleId, sizeof(titleId), "%s###%s", options.title, options.popupId);

    bool picked = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(EditorStyle::px(520.0f), EditorStyle::px(400.0f)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(EditorStyle::px(380.0f), EditorStyle::px(260.0f)),
        ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal(titleId, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextDisabled("%s", options.root.string().c_str());
        // Say so when the listing was cut short. A partial list that looks
        // complete is the same failure as an empty one with no warning: the
        // user concludes the file is not there.
        if (m_truncated) {
            ImGui::SameLine(0, EditorStyle::px(12.0f));
            ImGui::TextColored(EditorStyle::WARNING,
                               "first %d only - narrow the search", options.maxResults);
        }
        if (!options.hint.empty()) {
            ImGui::SameLine(0, EditorStyle::px(12.0f));
            ImGui::TextDisabled("%s", options.hint.c_str());
        }

        // Live filter. Focused on open so type-to-narrow needs no click;
        // Enter here confirms the selection (or the lone match).
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        const bool searchCommit = ImGui::InputTextWithHint("##pickerFilter", "Search...",
            m_filter, sizeof(m_filter), ImGuiInputTextFlags_EnterReturnsTrue);

        // Filtered view over the cached listing (indices into the full lists).
        std::vector<int> view;
        view.reserve(m_entries.size());
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (matchesFilter(m_entries[i].c_str(), m_filter)) view.push_back(i);
        }

        auto confirm = [&](int idx) {
            outPath = options.relativeTo.empty() ? m_paths[idx].string() : m_entries[idx];
            picked  = true;
            ImGui::CloseCurrentPopup();
        };

        // The list stretches; the button row keeps its one-row footprint.
        const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("##AssetPickerList", ImVec2(0, -footerH),
                              ImGuiChildFlags_Borders)) {
            if (view.empty()) {
                ImGui::TextDisabled(m_entries.empty() ? "(no matching entries)"
                                                      : "(nothing matches the search)");
            } else {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(view.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const int i = view[row];
                        const bool sel = (i == m_selected);
                        if (ImGui::Selectable(m_entries[i].c_str(), sel,
                                              ImGuiSelectableFlags_AllowDoubleClick)) {
                            m_selected = i;
                            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) confirm(i);
                        }
                    }
                }
            }
        }
        ImGui::EndChild();

        // Enter from the search field: the selection, or the single match.
        if (!picked && searchCommit) {
            if (m_selected >= 0 && matchesFilter(m_entries[m_selected].c_str(), m_filter)) {
                confirm(m_selected);
            } else if (view.size() == 1) {
                confirm(view[0]);
            }
        }

        if (!picked) {
            bool want = true;  // lifetime is popup-managed; the flag is discarded
            const DialogResult r = dialogButtons(want, "Open", m_selected >= 0);
            if (r == DialogResult::Confirm) confirm(m_selected);
        }
        ImGui::EndPopup();
    }
    return picked;
}

} // namespace Engine
