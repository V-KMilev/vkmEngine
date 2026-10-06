#define VKM_LOG_CATEGORY "EDITOR"

#include "session/build_controller.h"

#include <cstddef>
#include <system_error>

#include <imgui.h>

#include "logger.h"

#include "editor_state.h"
#include "io/project_paths.h"
#include "ui/editor_style.h"

namespace fs = std::filesystem;

namespace Vkm::Engine {

namespace {

/// Kept lines; a build's output past this sheds its oldest.
constexpr size_t MAX_LINES = 4000;

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
    const fs::path root = ProjectPaths::engineRoot();
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
    m_reveal = true;
    m_follow = true;

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
    if (!state.requestVkm.empty()) {
        run(std::move(state.requestVkm), std::move(state.requestVkmOpens));
        state.requestVkm.clear();
        state.requestVkmOpens.clear();
    }

    // A project that opens with no module is built once, so its code runs without a terminal.
    if (state.projectOpen && ProjectPaths::projectRoot() != m_checkedRoot) {
        m_checkedRoot = ProjectPaths::projectRoot();
        std::error_code ec;
        const bool buildable = fs::exists(m_checkedRoot / "CMakeLists.txt", ec);
        const bool unbuilt = !modulePath.empty() && !fs::exists(modulePath, ec);
        if (buildable && unbuilt && m_status != Status::Running) run({"build", m_checkedRoot.string()});
    }

    if (m_status != Status::Running) return;
    append(m_process.takeOutput());
    if (m_process.running()) return;

    append(m_process.takeOutput());
    if (!m_partial.empty()) append("\n");
    const int code = m_process.exitCode();
    m_status = code == 0 ? Status::Succeeded : Status::Failed;
    if (code != 0) {
        state.pushToast(ToastKind::Error, m_command + " failed - see the Build window");
        return;
    }
    if (!m_openWhenDone.empty()) {
        state.requestSceneAction(EditorState::SceneAction::OpenProject, m_openWhenDone);
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

void BuildController::draw(EditorState& state) {
    const bool running = m_status == Status::Running;
    const bool canRun = state.projectOpen && !running;
    const std::string root = ProjectPaths::projectRoot().string();

    ImGui::BeginDisabled(!canRun);
    if (ImGui::Button("Build")) state.requestVkm = {"build", root};
    ImGui::SameLine();
    if (ImGui::Button("Package")) state.requestVkm = {"package", root};
    ImGui::EndDisabled();
    ImGui::SameLine();
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
            const bool error = line.find("error") != std::string::npos;
            const bool warning = line.find("warning") != std::string::npos;
            const bool marked = error || warning;
            const ImVec4& colour = error ? EditorStyle::DANGER : EditorStyle::WARNING;
            if (marked) ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::TextUnformatted(line.c_str());
            if (marked) ImGui::PopStyleColor();
        }
    }
    // Follows the output while scrolled to its end, as last frame measured it; scrolling
    // up to read stops it. A new run starts at the end.
    if (m_follow || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    m_follow = false;
    ImGui::EndChild();
}

} // namespace Vkm::Engine
