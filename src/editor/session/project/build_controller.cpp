#define VKM_LOG_CATEGORY "EDITOR"

#include "session/project/build_controller.h"

#include <cstddef>
#include <system_error>

#include <imgui.h>

#include "vkm_log/logger.h"

#include "editor_state.h"
#include "io/project/roots.h"
#include "ui/style/editor_style.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

namespace {

constexpr size_t MAX_LINES = 4000;  ///< Kept lines; a build's output past this sheds its oldest.

#if defined(_WIN32)
constexpr const char* LAUNCHER = "vkm.cmd";
#else
constexpr const char* LAUNCHER = "vkm";
#endif

/// A word as a person would type it, quoted when it holds a space.
std::string typed(const std::string& word) {
    return word.find(' ') == std::string::npos ? word : "\"" + word + "\"";
}

} // namespace

fs::path BuildController::launcher() {
    const fs::path root = Roots::engineRoot();
    std::error_code ec;
    for (const fs::path& candidate : {root / LAUNCHER, root / "tools" / LAUNCHER}) {
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

void BuildController::run(std::vector<std::string> args, std::string openWhenDone) {
    const fs::path vkm = launcher();
    m_lines.clear();
    m_partial.clear();
    m_command = "vkm";
    for (const std::string& arg : args) m_command += " " + typed(arg);
    m_openWhenDone = std::move(openWhenDone);
    m_reveal       = true;
    m_follow       = true;

    if (vkm.empty()) {
        m_lines.push_back("This engine has no vkm beside it to run.");
        m_status = Status::Failed;
        return;
    }
    LOG_INFO("Running %s", m_command.c_str());
    m_status = m_process.start(vkm, args) ? Status::Running : Status::Failed;
    append(m_process.takeOutput());
}

void BuildController::update(EditorState& state, const fs::path& modulePath) {
    if (std::optional<VkmRun> asked = state.requests.take<VkmRun>()) {
        // One at a time: a second would race the first over a project half made or a build half linked.
        if (m_status == Status::Running) {
            state.toasts.push(ToastKind::Warning, m_command + " is still running", ConsoleView::Build);
        } else {
            run(std::move(asked->words), std::move(asked->opens));
        }
    }

    // A project that opens with no module is built once, so its code runs without a terminal: by vkm,
    // which builds into its bin/, so not an example whose module this engine's tree builds.
    if (state.projectOpen && Roots::projectRoot() != m_checkedRoot) {
        m_checkedRoot = Roots::projectRoot();
        std::error_code ec;
        const bool buildable = fs::exists(m_checkedRoot / "CMakeLists.txt", ec)
            && modulePath.parent_path() == Roots::projectBin();
        const bool unbuilt = !modulePath.empty() && !fs::exists(modulePath, ec);
        if (buildable && unbuilt && m_status != Status::Running) run({"build", m_checkedRoot.string()});
    }

    if (m_status != Status::Running) return;
    append(m_process.takeOutput());
    if (m_process.running()) return;

    append(m_process.takeOutput());
    if (!m_partial.empty()) append("\n");
    const int code = m_process.exitCode();
    m_status  = code == 0 ? Status::Succeeded : Status::Failed;
    m_endedAt = ImGui::GetTime();
    if (code != 0) {
        state.toasts.push(ToastKind::Error, m_command + " failed", ConsoleView::Build);
        return;
    }
    if (!m_openWhenDone.empty()) {
        state.sceneActions.request(SceneActions::Kind::OpenProject, m_openWhenDone);
        m_openWhenDone.clear();
    }
}

bool BuildController::takeReveal() {
    const bool reveal = m_reveal;
    m_reveal = false;
    return reveal;
}

void BuildController::append(const std::string& text) {
    for (const char c : text) {
        if (c == '\n') {
            m_lines.push_back(std::move(m_partial));
            m_partial.clear();
        } else if (c == '\r') {
            m_partial.clear();
        } else {
            m_partial += c;
        }
    }
    if (m_lines.size() > MAX_LINES) {
        const auto shed = static_cast<std::ptrdiff_t>(m_lines.size() - MAX_LINES);
        m_lines.erase(m_lines.begin(), m_lines.begin() + shed);
    }
}

void BuildController::draw() {
    const bool running = m_status == Status::Running;
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop")) {
        m_process.stop();
        m_status = Status::Stopped;
        m_lines.push_back("Stopped.");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    const char* cmd = m_command.c_str();
    switch (m_status) {
        case Status::Idle:      ImGui::TextDisabled("Nothing run yet"); break;
        case Status::Running:   ImGui::TextDisabled("%s ...", cmd); break;
        case Status::Succeeded: ImGui::TextColored(EditorStyle::SUCCESS, "%s - done", cmd); break;
        case Status::Failed:    ImGui::TextColored(EditorStyle::DANGER, "%s - failed", cmd); break;
        case Status::Stopped:   ImGui::TextColored(EditorStyle::WARNING, "%s - stopped", cmd); break;
    }

    ImGui::Separator();
    const bool output = ImGui::BeginChild(
        "##output",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_HorizontalScrollbar
    );
    if (!output) {
        ImGui::EndChild();
        return;
    }
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_lines.size()) + (m_partial.empty() ? 0 : 1));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::string& line = i < static_cast<int>(m_lines.size()) ? m_lines[i] : m_partial;
            const bool error   = line.find("error") != std::string::npos;
            const bool warning = line.find("warning") != std::string::npos;
            const bool marked  = error || warning;
            const ImVec4& color = error ? EditorStyle::DANGER : EditorStyle::WARNING;
            if (marked) ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(line.c_str());
            if (marked) ImGui::PopStyleColor();
        }
    }
    // Follows the output while scrolled to its end, as last frame measured it; a new run starts there.
    if (m_follow || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    m_follow = false;
    ImGui::EndChild();
}

} // namespace Vkm::Engine
