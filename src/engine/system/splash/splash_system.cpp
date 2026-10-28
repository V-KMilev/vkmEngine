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

// @p caption if it is there; a missing one is warned of and shown as none.
std::string captionThere(const std::string& caption) {
    std::error_code ec;
    if (caption.empty() || std::filesystem::exists(caption, ec)) return caption;
    LOG_WARNING("Splash caption '%s' is not there", caption.c_str());
    return {};
}

// The most one frame may advance the sequence: a first frame that stalls on a load would
// otherwise spend the fade before a logo is drawn. Frames merely slow - the driver compiling
// shaders on every core - keep the clock's time, or the sequence would last as long as they do.
constexpr float MAX_STEP_SECONDS = 0.25f;

} // namespace

void SplashSystem::add(const std::string& path, float hold, const std::string& caption) {
    addEntry(path, hold, caption, false);
}

void SplashSystem::addIdent(const std::string& path, const std::string& caption) {
    addEntry(path, 0.0f, caption, true);
}

void SplashSystem::addEntry(const std::string& path, float hold, const std::string& caption, bool reveal) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        LOG_WARNING("Splash image '%s' is not there; skipping it", path.c_str());
        return;
    }
    m_entries.push_back({path, hold, captionThere(caption), reveal});
}

float SplashSystem::fadeOf(const Entry& entry) const {
    // An ident lasts as long whatever the fade: the backend's reveal is timed by its length.
    return entry.reveal ? std::min(m_fade, 0.5f * IDENT_SECONDS) : m_fade;
}

float SplashSystem::spanOf(const Entry& entry) const {
    const float fade = fadeOf(entry);
    const float hold = entry.reveal ? IDENT_SECONDS - 2.0f * fade : entry.hold;
    return fade + hold + fade;
}

void SplashSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SplashSystem");

    // Cleared on every path, so a frame past the sequence reads as no splash.
    ctx.splash = &m_frame;
    m_frame.key.clear();   // keeps the capacity
    m_frame.caption.clear();
    m_frame.reveal  = false;
    m_frame.opacity = 0.0f;

    if (m_current >= m_entries.size()) return;

    m_elapsed += std::min(ctx.clock.getDeltaTime(), MAX_STEP_SECONDS);

    // Walked, not one entry per frame: a frame that finished an entry without taking
    // the next would read as no splash and flash the world.
    while (m_current < m_entries.size()) {
        const float span = spanOf(m_entries[m_current]);
        if (m_elapsed < span) break;
        // The remainder carries, so a sequence lasts the sum of its entries; a
        // zero-length entry still advances, so the walk ends.
        m_elapsed -= span;
        ++m_current;
    }
    if (m_current >= m_entries.size()) return;

    const Entry& entry = m_entries[m_current];
    const float  total = spanOf(entry);
    const float  fade  = fadeOf(entry);

    const float rising  = fade > 0.0f ? glm::clamp(m_elapsed / fade, 0.0f, 1.0f) : 1.0f;
    const float falling = fade > 0.0f ? glm::clamp((total - m_elapsed) / fade, 0.0f, 1.0f) : 1.0f;

    // Eased: the backbuffer is not sRGB-encoded, so a linear ramp reads as an appearance, not a fade.
    m_frame.key     = entry.path;
    m_frame.reveal  = entry.reveal;
    m_frame.caption = entry.caption;
    m_frame.opacity = glm::smoothstep(0.0f, 1.0f, std::min(rising, falling));
    m_frame.time    = m_elapsed;
}

} // namespace Vkm::Engine
