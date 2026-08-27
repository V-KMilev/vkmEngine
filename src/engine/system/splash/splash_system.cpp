#define VKM_LOG_CATEGORY "SPLASH"

#include "system/splash/splash_system.h"

#include <algorithm>
#include <filesystem>

#include <glm/common.hpp>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"
#include "io/project.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

namespace {

// The most one frame may advance the sequence, in seconds. The first frames of
// the process carry every shader compile, every upload and the scene load, and
// counting one whole would spend the fade before the logo is drawn once; a long
// load makes the logo stay up rather than skip. Anything slower than 20fps is a
// hitch, so an ordinary frame is never clamped.
constexpr float MAX_STEP_SECONDS = 1.0f / 20.0f;

} // namespace

void SplashSystem::add(const std::string& path, float hold) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_WARNING("Splash image '%s' is not there; skipping it", path.c_str());
        return;
    }
    m_entries.push_back({path, hold});
}

void SplashSystem::init(FrameContext&) {
    Project project;
    loadProject(ProjectPaths::projectRoot(), project);
    for (const SplashEntry& entry : project.splash) {
        add(ProjectPaths::resolveProjectPath(entry.image).string(), entry.seconds);
    }
}

void SplashSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SplashSystem");

    // Published on every path, empty included: a frame that publishes nothing
    // is one the editor reads as "the splash is over" and paints its chrome
    // into.
    ctx.splash = &m_frame;
    m_frame = SplashFrame{};

    if (m_current >= m_entries.size()) return;

    const Entry& entry = m_entries[m_current];
    const float  total = m_fade + entry.hold + m_fade;

    // The frame clock, deliberately. The simulation is paused behind this in
    // the editor, and a splash that waited for the tick would never fade.
    m_elapsed += std::min(ctx.clock.getDeltaTime(), MAX_STEP_SECONDS);
    if (m_elapsed >= total) {
        ++m_current;
        m_elapsed = 0.0f;
        return;
    }

    const float rising  = m_fade > 0.0f ? glm::clamp(m_elapsed / m_fade, 0.0f, 1.0f) : 1.0f;
    const float falling = m_fade > 0.0f ? glm::clamp((total - m_elapsed) / m_fade, 0.0f, 1.0f) : 1.0f;

    // Eased, not linear. The backbuffer is not sRGB-encoded on write, so a
    // linear ramp is linear on the glass, and a white mark over black then
    // reads as an appearance rather than as a fade.
    m_frame.key     = entry.path;
    m_frame.opacity = glm::smoothstep(0.0f, 1.0f, std::min(rising, falling));
}

} // namespace Vkm::Engine
