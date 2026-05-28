#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/asset_picker.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "logger.h"

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

    std::error_code ec;
    auto consider = [&](const std::filesystem::directory_entry& e) -> bool {
        if (static_cast<int>(m_paths.size()) >= options.maxResults) return false;
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

    ImGui::OpenPopup(options.popupId);
}

bool AssetPicker::draw(std::string& outPath) {
    refreshIfNeeded();

    bool picked = false;
    if (ImGui::BeginPopupModal(options.popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", options.root.string().c_str());
        if (!options.hint.empty()) {
            ImGui::SameLine(0, 12);
            ImGui::TextDisabled("%s", options.hint.c_str());
        }
        ImGui::Separator();

        if (m_entries.empty()) {
            ImGui::TextDisabled("(no matching entries)");
        } else {
            ImGui::BeginChild("##AssetPickerList", ImVec2(420, 240), true);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_entries.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    if (ImGui::Selectable(m_entries[i].c_str())) {
                        if (!options.relativeTo.empty()) {
                            outPath = m_entries[i];
                        } else {
                            outPath = m_paths[i].string();
                        }
                        picked = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
        }

        ImGui::Separator();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return picked;
}

} // namespace Engine
