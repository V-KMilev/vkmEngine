#include "panels/errors_panel.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include <imgui.h>

#include "debug/engine_error_log.h"

namespace Vkm::Engine {

void drawErrorsPanel(EngineErrorLog& errorLog) {
    const auto& entries = errorLog.entries();
    ImGui::Text(
        "%zu entr%s (newest first, cap %zu)",
        entries.size(),
        entries.size() == 1 ? "y" : "ies",
        EngineErrorLog::CAPACITY
    );
    ImGui::SameLine();
    if (ImGui::Button("Clear")) errorLog.clearAll();
    ImGui::Separator();

    if (entries.empty()) {
        ImGui::TextDisabled(
            "No errors. Recoverable engine failures (e.g. a script hook that throws) are recorded here."
        );
        return;
    }

    const bool listOpen = ImGui::BeginChild(
        "##engine_err_list",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_None,
        ImGuiWindowFlags_HorizontalScrollbar
    );
    if (listOpen) {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            const auto& e = *it;
            const auto tt = std::chrono::system_clock::to_time_t(e.timestamp);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            char ts[16];
            std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);

            char header[192];
            if (e.repeatCount > 1) {
                std::snprintf(
                    header,
                    sizeof(header),
                    "[%s] [%s] %s  x%u",
                    ts,
                    e.category.c_str(),
                    e.source.c_str(),
                    e.repeatCount
                );
            } else {
                std::snprintf(
                    header,
                    sizeof(header),
                    "[%s] [%s] %s",
                    ts,
                    e.category.c_str(),
                    e.source.c_str()
                );
            }
            ImGui::PushID(&e);
            if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("%s", e.message.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

} // namespace Vkm::Engine
