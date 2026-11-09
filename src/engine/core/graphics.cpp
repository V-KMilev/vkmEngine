#define VKM_LOG_CATEGORY "RENDER"

#include "core/graphics.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

#include "vkm_log/assert.h"
#include "vkm_log/logger.h"

#include "io/json/own_file.h"

namespace Vkm::Engine {

namespace {

constexpr const char* WINDOW_KEY = "window";  ///< The key the player's window mode is kept under, beside the
                                              ///< render choices.

/**
 * @brief What a tier sets; the rest of the settings it leaves as they are.
 */
struct Tier {
    uint32_t msaa;
    uint32_t shadows;
    bool     gtao;
    bool     ssr;
    bool     bloom;
    uint32_t anisotropy;
    bool     softShadows;
    float    renderScale;
    bool     ssrHalf;
};

// Performance to Max, in GraphicsPreset order.
constexpr Tier TIERS[] = {
    {1, 1024, false, false, false, 4,  false, 0.67f, true},   // Performance
    {1, 2048, false, false, true,  8,  false, 1.0f,  true},   // Low
    {2, 2048, true,  false, true,  8,  false, 1.0f,  true},   // Medium
    {2, 4096, true,  true,  true,  16, true,  1.0f,  true},   // High
    {4, 4096, true,  true,  true,  16, true,  1.0f,  false},  // Max
};
static_assert(std::size(TIERS) == static_cast<size_t>(GraphicsPreset::Count));

/**
 * @brief What a tier sets, one (json-key, Tier member, RenderSettings member) row each.
 *
 * @tparam Fn Called as f(key, pointer to the Tier member, pointer to the RenderSettings member).
 */
template <typename Fn>
void visitTierFields(Fn&& f) {
    f("msaaSamples",       &Tier::msaa,        &RenderSettings::msaaSamples);
    f("shadowResolution",  &Tier::shadows,     &RenderSettings::shadowResolution);
    f("textureAnisotropy", &Tier::anisotropy,  &RenderSettings::textureAnisotropy);
    f("renderScale",       &Tier::renderScale, &RenderSettings::renderScale);
    f("ssrHalfResolution", &Tier::ssrHalf,     &RenderSettings::ssrHalfResolution);
    f("gtao",              &Tier::gtao,        &RenderSettings::gtao);
    f("ssr",               &Tier::ssr,         &RenderSettings::ssr);
    f("bloom",             &Tier::bloom,       &RenderSettings::bloom);
    f("softShadows",       &Tier::softShadows, &RenderSettings::softShadows);
}

/// @p settings with what @p preset sets, its effects as the tier has them.
RenderSettings withTier(RenderSettings settings, GraphicsPreset preset) {
    VKM_ASSERT(preset < GraphicsPreset::Count, "Graphics takes a tier, not the Count sentinel");
    const Tier& tier = TIERS[std::min(static_cast<size_t>(preset), std::size(TIERS) - 1)];
    visitTierFields([&](const char*, auto from, auto to) { settings.*to = tier.*from; });
    return settings;
}

/// @p settings with each effect on only where @p project has it on.
RenderSettings keptTo(const RenderSettings& project, RenderSettings settings) {
    settings.gtao        = project.gtao && settings.gtao;
    settings.ssr         = project.ssr && settings.ssr;
    settings.bloom       = project.bloom && settings.bloom;
    settings.softShadows = project.softShadows && settings.softShadows;
    return settings;
}

/**
 * @brief What a player chooses, one (json-key, member) row each: what a tier sets, and the cull.
 *
 * @tparam Fn Called as f(key, pointer to the RenderSettings member) per field.
 */
template <typename Fn>
void visitChoices(Fn&& f) {
    visitTierFields([&](const char* key, auto, auto field) { f(key, field); });
    f("occlusionCulling", &RenderSettings::occlusionCulling);
}

bool sameChoices(const RenderSettings& a, const RenderSettings& b) {
    bool same = true;
    visitChoices([&](const char*, auto field) { same = same && a.*field == b.*field; });
    return same;
}

/// Whether the renderer takes @p settings' choices as they are; a file can hold anything.
bool takenAsIs(const RenderSettings& settings) {
    const auto shadows = std::find(
        std::begin(RenderSettings::SHADOW_RESOLUTIONS),
        std::end(RenderSettings::SHADOW_RESOLUTIONS),
        settings.shadowResolution
    );
    return RenderSettings::isMsaaSampleCount(settings.msaaSamples)
        && shadows != std::end(RenderSettings::SHADOW_RESOLUTIONS)
        && settings.renderScale >= RenderSettings::MIN_RENDER_SCALE
        && settings.renderScale <= RenderSettings::MAX_RENDER_SCALE
        && settings.textureAnisotropy >= 1;
}

} // namespace

void Graphics::set(const RenderSettings& settings) {
    RenderSettings next  = keptTo(m_project, settings);
    const bool     chose = !m_choicesFile.empty() && !sameChoices(next, m_settings);
    m_settings = std::move(next);
    if (chose) writeChoices();
}

void Graphics::setQuality(GraphicsPreset tier) {
    set(withTier(m_settings, tier));
}

std::optional<GraphicsPreset> Graphics::quality() const {
    std::optional<GraphicsPreset> highest;
    for (size_t i = 0; i < std::size(TIERS); ++i) {
        const auto tier = static_cast<GraphicsPreset>(i);
        if (sameChoices(keptTo(m_project, withTier(m_settings, tier)), m_settings)) highest = tier;
    }
    return highest;
}

void Graphics::setWindowMode(Platform::WindowMode mode) {
    m_windowRequest = mode;
    if (m_windowMode == mode) return;
    m_windowMode = mode;
    if (!m_choicesFile.empty()) writeChoices();
}

void Graphics::setProject(const RenderSettings& project) {
    m_project  = project;
    m_settings = project;
}

void Graphics::keepChoicesIn(std::filesystem::path file) {
    m_choicesFile = std::move(file);
    const nlohmann::json doc = readOwnFile(m_choicesFile);
    RenderSettings chosen    = m_settings;
    visitChoices([&](const char* key, auto field) {
        const auto held = doc.find(key);
        if (held == doc.end()) return;
        using Field = std::remove_reference_t<decltype(chosen.*field)>;
        try {
            chosen.*field = held->template get<Field>();
        } catch (const nlohmann::json::exception&) {
            LOG_WARNING(
                "'%s' holds %s as '%s'; keeping the project's",
                m_choicesFile.string().c_str(),
                key,
                held->dump().c_str()
            );
        }
    });
    // The window apart: a mode this build does not name keeps the host's.
    if (const auto held = doc.find(WINDOW_KEY); held != doc.end()) {
        Platform::WindowMode mode = Platform::WindowMode::Windowed;
        if (held->is_string() && Reflect::enumFromNameChecked(held->get<std::string>(), mode)) {
            m_windowMode = mode;
        } else {
            LOG_WARNING(
                "'%s' holds %s as '%s'; keeping the host's",
                m_choicesFile.string().c_str(),
                WINDOW_KEY,
                held->dump().c_str()
            );
        }
    }
    // Refused whole rather than clamped: a value the renderer does not take was not the player's.
    if (!takenAsIs(chosen)) {
        LOG_WARNING(
            "'%s' holds a choice the renderer does not take; keeping the project's",
            m_choicesFile.string().c_str()
        );
        return;
    }
    m_settings = keptTo(m_project, chosen);
}

void Graphics::writeChoices() const {
    nlohmann::json doc = nlohmann::json::object();
    visitChoices([&](const char* key, auto field) { doc[key] = m_settings.*field; });
    if (m_windowMode) doc[WINDOW_KEY] = Reflect::enumName(*m_windowMode);
    writeOwnFile(m_choicesFile, std::move(doc));
}

} // namespace Vkm::Engine
