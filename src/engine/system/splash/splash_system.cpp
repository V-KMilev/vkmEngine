#define VKM_LOG_CATEGORY "SPLASH"

#include "system/splash/splash_system.h"

#include <algorithm>
#include <filesystem>

#include <glm/common.hpp>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"

namespace Vkm::Engine {

namespace {

// The most one frame may advance the sequence: the first frames carry shader
// compiles and loads, and would otherwise spend the fade before a logo is drawn.
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

void SplashSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SplashSystem");

    // Cleared on every path, so a frame past the sequence reads as no splash.
    ctx.splash = &m_frame;
    m_frame.key.clear();   // keeps the capacity
    m_frame.opacity = 0.0f;

    if (m_current >= m_entries.size()) return;

    m_elapsed += std::min(ctx.clock.getDeltaTime(), MAX_STEP_SECONDS);

    // Walked, not one entry per frame: a frame that finished an entry without taking
    // the next would read as no splash and flash the world.
    while (m_current < m_entries.size()) {
        const float span = m_fade + m_entries[m_current].hold + m_fade;
        if (m_elapsed < span) break;
        // The remainder carries, so a sequence lasts the sum of its entries; a
        // zero-length entry still advances, so the walk ends.
        m_elapsed -= span;
        ++m_current;
    }
    if (m_current >= m_entries.size()) return;

    const Entry& entry = m_entries[m_current];
    const float  total = m_fade + entry.hold + m_fade;

    const float rising  = m_fade > 0.0f ? glm::clamp(m_elapsed / m_fade, 0.0f, 1.0f) : 1.0f;
    const float falling = m_fade > 0.0f ? glm::clamp((total - m_elapsed) / m_fade, 0.0f, 1.0f) : 1.0f;

    // Eased: the backbuffer is not sRGB-encoded, so a linear ramp reads as an appearance, not a fade.
    m_frame.key     = entry.path;
    m_frame.opacity = glm::smoothstep(0.0f, 1.0f, std::min(rising, falling));
}

} // namespace Vkm::Engine
