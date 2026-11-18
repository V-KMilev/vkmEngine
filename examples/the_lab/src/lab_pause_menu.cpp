#include "lab_pause_menu.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include "base/event/event_bus.h"
#include "core/graphics.h"
#include "base/reflect/enum_names.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "core/input_map.h"
#include "ecs/component/ui/ui_build.h"
#include "core/frame/ui_events.h"

#include "lab_actions.h"
#include "lab_palette.h"

namespace Lab {

using namespace Vkm::Platform;

namespace {

const glm::vec4 GRAY  {0.62f, 0.68f, 0.78f, 1.00f};
const glm::vec4 ACCENT{0.14f, 0.62f, 0.86f, 0.96f};
const glm::vec4 HOT   {0.24f, 0.78f, 1.00f, 0.98f};
const glm::vec4 OFF   {0.22f, 0.25f, 0.32f, 0.96f};

const glm::vec2 BUTTON{240.0f, 52.0f};

// A settings row is its name, at the left of the settings column; its control rides the right end.
const glm::vec2 ROW{500.0f, 40.0f};
const UIElement LABEL  = UIElement::at({0.0f, 0.5f}, {0.0f, 0.0f}, ROW);
const UIElement CHOICE = UIElement::at({1.0f, 0.5f}, {0.0f, 0.0f}, {240.0f, ROW.y});
const UIElement SWITCH = UIElement::at({1.0f, 0.5f}, {-78.0f, 0.0f}, {84.0f, 36.0f});

constexpr float ROW_GAP         = 6.0f;
constexpr float ROWS_TOP        = 92.0f;
constexpr float BACK_GAP        = 14.0f;  // more room above the Back button than between the rows
constexpr float RENDER_SCALES[] = {0.5f, 0.67f, 0.85f, 1.0f};

/**
 * @brief The index of the entry of @p values nearest @p value.
 *
 * @param values What a row steps through.
 * @param value  What the frame renders with.
 * @return The row's index for it.
 */
template <typename T, size_t N>
int nearestIndex(const T (&values)[N], T value) {
    size_t best = 0;
    for (size_t i = 1; i < N; ++i) {
        const double gap  = std::fabs(static_cast<double>(values[i]) - static_cast<double>(value));
        const double held = std::fabs(static_cast<double>(values[best]) - static_cast<double>(value));
        if (gap < held) best = i;
    }
    return static_cast<int>(best);
}

std::vector<std::string> qualityNames() {
    std::vector<std::string> names;
    for (size_t i = 0; i < static_cast<size_t>(GraphicsPreset::Count); ++i) {
        names.emplace_back(Reflect::enumName(static_cast<GraphicsPreset>(i)));
    }
    return names;
}

std::vector<std::string> antiAliasingNames() {
    std::vector<std::string> names;
    for (const uint32_t samples : RenderSettings::MSAA_SAMPLE_COUNTS) {
        names.push_back(samples == 1 ? "Off" : std::to_string(samples) + "x MSAA");
    }
    return names;
}

std::vector<std::string> shadowNames() {
    std::vector<std::string> names;
    for (const uint32_t resolution : RenderSettings::SHADOW_RESOLUTIONS) {
        names.push_back(std::to_string(resolution));
    }
    return names;
}

std::vector<std::string> renderScaleNames() {
    std::vector<std::string> names;
    for (const float scale : RENDER_SCALES) {
        names.push_back(std::to_string(std::lround(scale * 100.0f)) + "%");
    }
    return names;
}

} // namespace

void LabPauseMenu::onStart() {

    // Offline only: in a session the clock is the session's, and a restart would load a scene
    // under every player.
    if (!net().isOffline()) return;

    // Built now, while time runs: a behavior spawned while paused would never start. Shown, the
    // canvas holds the world and frees the cursor, and the menu action opens and closes it.
    m_canvas = UI::canvas(scene(), "Pause Menu", LAYER_MENU);
    UICanvas& canvas = scene().get<UICanvas>(m_canvas);

    canvas.visible      = false;
    canvas.pausesGame   = true;
    canvas.toggleAction = ACTION_MENU;

    // The whole screen, faded as one.
    m_menu = spawn("Menu", m_canvas);
    UIElement menu;
    menu.size         = {0.0f, 0.0f};
    menu.relativeSize = {1.0f, 1.0f};
    scene().add(m_menu, std::move(menu));

    const auto page = [&](const char* name, const char* title, const glm::vec2& size) {
        const EntityId id = spawn(name, m_menu);
        scene().add(id, UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, size));
        scene().add(id, UIImage{INK, {}, {}});

        const EntityId heading = spawn("Title", id);
        scene().add(heading, UIElement::at({0.5f, 0.0f}, {0.0f, 28.0f}, {size.x - 40.0f, 48.0f}));
        UIText text;
        text.text      = title;
        text.pixelSize = 40.0f;
        text.color     = WHITE;
        text.align     = UIText::Align::Center;
        scene().add(heading, std::move(text));
        return id;
    };

    m_style.button = ACCENT;
    m_style.hover  = HOT;
    m_style.accent = ACCENT;
    m_style.text   = WHITE;
    const auto button = [&](EntityId parent, const char* caption, float y) {
        return UI::button(scene(), parent, UIElement::at({0.5f, 0.0f}, {0.0f, y}, BUTTON), caption, m_style);
    };

    m_main = page("Main", "PAUSED", {420.0f, 330.0f});
    onClick(button(m_main, "RESUME", 100.0f), [this] { close(); });
    onClick(button(m_main, "SETTINGS", 164.0f), [this] { showSettings(true); });
    // The restart takes the menu with the old world, so the new one runs.
    onClick(button(m_main, "RESTART", 228.0f), [this] { restartScene(); });

    const EntityId hint = spawn("Hint", m_main);
    scene().add(hint, UIElement::at({0.5f, 0.0f}, {0.0f, 292.0f}, {380.0f, 22.0f}));
    UIText hintText;
    hintText.text      = input().keyName(ACTION_MENU) + " closes this too";
    hintText.pixelSize = 15.0f;
    hintText.color     = GRAY;
    hintText.align     = UIText::Align::Center;
    scene().add(hint, std::move(hintText));

    const UIElement rows = UIElement::at({0.5f, 0.0f}, {0.0f, ROWS_TOP}, {ROW.x, 0.0f});

    m_settings = page("Settings", "SETTINGS", {560.0f, 656.0f});
    m_rows     = UI::column(scene(), m_settings, rows, ROW_GAP);

    m_quality      = addChoice("Quality", qualityNames());
    m_antiAliasing = addChoice("Anti-aliasing", antiAliasingNames());
    m_shadows      = addChoice("Shadow detail", shadowNames());
    m_renderScale  = addChoice("Render scale", renderScaleNames());
    m_bloom        = addToggle("Bloom");
    m_occlusion    = addToggle("Ambient occlusion");
    m_reflections  = addToggle("Reflections");
    m_softShadows  = addToggle("Soft shadows");
    m_culling      = addToggle("Occlusion culling");
    m_fullscreen   = addToggle("Fullscreen");
    onClick(button(m_rows, "BACK", BACK_GAP), [this] { showSettings(false); });

    showInEffect();
    showSettings(false);

    // A setting applies as it is changed, and the page then shows what the frame draws: a row the
    // project has off stays off.
    onChange(m_quality, [this](float tier) {
        graphics().setQuality(static_cast<GraphicsPreset>(std::lround(tier)));
        showInEffect();
    });
    for (const EntityId setting : {
             m_antiAliasing, m_shadows, m_renderScale, m_bloom, m_occlusion, m_reflections, m_softShadows,
             m_culling
         }) {
        onChange(setting, [this](float) {
            apply();
            showInEffect();
        });
    }
    // The window is the player's choice too, kept with the rest.
    onChange(m_fullscreen, [this](float on) {
        graphics().setWindowMode(on > 0.5f ? WindowMode::Fullscreen : WindowMode::Windowed);
    });
}

void LabPauseMenu::onRealtimeUpdate(float dt) {
    if (!net().isOffline()) return;

    // Closed, by its action or by Resume, it opens on the main page again.
    const bool open = scene().get<UICanvas>(m_canvas).visible;
    if (!open) showSettings(false);

    const float target = open ? 1.0f : 0.0f;
    const float step   = fadeSeconds > 0.0f ? dt / fadeSeconds : 1.0f;
    m_fade = m_fade < target ? std::min(target, m_fade + step) : std::max(target, m_fade - step);

    if (UIElement* menu = scene().tryGet<UIElement>(m_menu)) menu->opacity = m_fade;
}

void LabPauseMenu::close() {
    scene().get<UICanvas>(m_canvas).visible = false;
}

void LabPauseMenu::showSettings(bool settings) {
    scene().get<UIElement>(m_main).visible     = !settings;
    scene().get<UIElement>(m_settings).visible = settings;
}

EntityId LabPauseMenu::addChoice(const char* name, std::vector<std::string> options) {
    const EntityId row = UI::text(scene(), m_rows, LABEL, name, m_style);
    return UI::choice(scene(), row, CHOICE, std::move(options), 0, m_style);
}

EntityId LabPauseMenu::addToggle(const char* name) {
    UI::Style off = m_style;
    off.button = OFF;
    const EntityId row = UI::text(scene(), m_rows, LABEL, name, m_style);
    return UI::toggle(scene(), row, SWITCH, false, off);
}

void LabPauseMenu::showInEffect() {
    // None when no tier is what the frame draws, which the row shows blank.
    const std::optional<GraphicsPreset> tier = graphics().quality();
    scene().get<UIChoice>(m_quality).index = tier ? static_cast<int>(*tier) : -1;
    showRows(graphics().settings());
    scene().get<UIToggle>(m_fullscreen).on = window() && window()->mode() == WindowMode::Fullscreen;
}

void LabPauseMenu::showRows(const RenderSettings& settings) {
    const auto show = [&](EntityId row, int index) { scene().get<UIChoice>(row).index = index; };
    const auto flip = [&](EntityId row, bool on) { scene().get<UIToggle>(row).on = on; };
    show(m_antiAliasing, nearestIndex(RenderSettings::MSAA_SAMPLE_COUNTS, settings.msaaSamples));
    show(m_shadows,      nearestIndex(RenderSettings::SHADOW_RESOLUTIONS, settings.shadowResolution));
    show(m_renderScale,  nearestIndex(RENDER_SCALES, settings.renderScale));
    flip(m_bloom,        settings.bloom);
    flip(m_occlusion,    settings.gtao);
    flip(m_reflections,  settings.ssr);
    flip(m_softShadows,  settings.softShadows);
    flip(m_culling,      settings.occlusionCulling);
}

void LabPauseMenu::apply() {
    const auto pick = [&](EntityId row, const auto& values) {
        const int index = scene().get<UIChoice>(row).index;
        return values[std::clamp(index, 0, static_cast<int>(std::size(values)) - 1)];
    };
    const auto on = [&](EntityId row) { return scene().get<UIToggle>(row).on; };

    RenderSettings r = graphics().settings();
    r.msaaSamples      = pick(m_antiAliasing, RenderSettings::MSAA_SAMPLE_COUNTS);
    r.shadowResolution = pick(m_shadows, RenderSettings::SHADOW_RESOLUTIONS);
    r.renderScale      = pick(m_renderScale, RENDER_SCALES);
    r.bloom            = on(m_bloom);
    r.gtao             = on(m_occlusion);
    r.ssr              = on(m_reflections);
    r.softShadows      = on(m_softShadows);
    r.occlusionCulling = on(m_culling);
    graphics().set(r);
}

} // namespace Lab
