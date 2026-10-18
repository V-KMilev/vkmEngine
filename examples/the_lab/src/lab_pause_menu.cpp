#include "lab_pause_menu.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

#include "core/clock.h"
#include "core/event/event_bus.h"
#include "core/reflect.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "system/ui/ui_events.h"

#include "lab_actions.h"
#include "lab_palette.h"

namespace Lab {

namespace {

const glm::vec4 GREY  {0.62f, 0.68f, 0.78f, 1.00f};
const glm::vec4 ACCENT{0.14f, 0.62f, 0.86f, 0.96f};
const glm::vec4 HOT   {0.24f, 0.78f, 1.00f, 0.98f};
const glm::vec4 OFF   {0.22f, 0.25f, 0.32f, 0.96f};

// A step of any of these is a change of setting, applied at once.
const std::string QUALITY       = "setting:quality";
const std::string ANTI_ALIASING = "setting:antiAliasing";
const std::string SHADOWS       = "setting:shadows";
const std::string RENDER_SCALE  = "setting:renderScale";
const std::string BLOOM         = "setting:bloom";
const std::string OCCLUSION     = "setting:occlusion";
const std::string REFLECTIONS   = "setting:reflections";
const std::string SOFT_SHADOWS  = "setting:softShadows";
const std::string CULLING       = "setting:culling";

constexpr float    ROW_WIDTH       = 500.0f;
constexpr float    ROW_HEIGHT      = 40.0f;
constexpr float    ROW_STEP        = 46.0f;
constexpr float    RENDER_SCALES[] = {0.5f, 0.67f, 0.85f, 1.0f};

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
    // Defined on every end, with or without the menu: see lab_actions.h.
    input().define(ACTION_MENU, { InputBinding{InputSource::Key, GLFW_KEY_ESCAPE, 1.0f} });

    // Offline only: in a session the clock is the session's, client spawns take
    // server-owned slots, and a restart would load a scene under every player.
    if (!net().isOffline()) return;

    // Built now, while time runs: a behavior spawned while paused would never start.
    m_canvas = spawn("Pause Menu");
    UICanvas canvas;
    canvas.sortOrder = LAYER_MENU;
    canvas.visible   = false;
    scene().add(m_canvas, std::move(canvas));

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

    m_main = page("Main", "PAUSED", {420.0f, 330.0f});
    addButton(m_main, "RESUME",   "lab:resume",   {0.0f, 100.0f}, {240.0f, 52.0f});
    addButton(m_main, "SETTINGS", "lab:settings", {0.0f, 164.0f}, {240.0f, 52.0f});
    addButton(m_main, "RESTART",  "lab:restart",  {0.0f, 228.0f}, {240.0f, 52.0f});

    const EntityId hint = spawn("Hint", m_main);
    scene().add(hint, UIElement::at({0.5f, 0.0f}, {0.0f, 292.0f}, {380.0f, 22.0f}));
    UIText hintText;
    hintText.text      = keyFor(input(), ACTION_MENU) + " closes this too";
    hintText.pixelSize = 15.0f;
    hintText.color     = GREY;
    hintText.align     = UIText::Align::Center;
    scene().add(hint, std::move(hintText));

    m_settings = page("Settings", "SETTINGS", {560.0f, 610.0f});
    float y = 92.0f;
    m_quality      = addChoice("Quality", QUALITY, qualityNames(), y);
    m_antiAliasing = addChoice("Anti-aliasing", ANTI_ALIASING, antiAliasingNames(), y += ROW_STEP);
    m_shadows      = addChoice("Shadow detail", SHADOWS, shadowNames(), y += ROW_STEP);
    m_renderScale  = addChoice("Render scale", RENDER_SCALE, renderScaleNames(), y += ROW_STEP);
    m_bloom        = addToggle("Bloom", BLOOM, y += ROW_STEP);
    m_occlusion    = addToggle("Ambient occlusion", OCCLUSION, y += ROW_STEP);
    m_reflections  = addToggle("Reflections", REFLECTIONS, y += ROW_STEP);
    m_softShadows  = addToggle("Soft shadows", SOFT_SHADOWS, y += ROW_STEP);
    m_culling      = addToggle("Occlusion culling", CULLING, y += ROW_STEP);
    addButton(m_settings, "BACK", "lab:back", {0.0f, y + ROW_STEP + 14.0f}, {240.0f, 52.0f});

    // The page opens on High without applying it: the frame keeps the project's settings until a
    // row changes.
    scene().get<UIChoice>(m_quality).index = static_cast<int>(GraphicsPreset::High);
    showTier(GraphicsPreset::High);
    showSettings(false);

    subscribe([this](const UIClickEvent& e) {
        if (e.eventId == "lab:resume") setOpen(false);
        else if (e.eventId == "lab:settings") showSettings(true);
        else if (e.eventId == "lab:back") showSettings(false);
        else if (e.eventId == "lab:restart") {
            // Unpause first: the clock survives the restart and this behavior does not.
            setOpen(false);
            restartScene();
        }
    });

    subscribe([this](const UIValueEvent& e) {
        if (e.eventId.rfind("setting:", 0) != 0) return;
        if (e.eventId == QUALITY) showTier(static_cast<GraphicsPreset>(std::lround(e.value)));
        apply();
    });
}

void LabPauseMenu::onRealtimeUpdate(float dt) {
    if (!net().isOffline()) return;

    // Opening lives here, not in onUpdate: both hooks see the same press, so an
    // onUpdate that opened would have this line close it again.
    if (input().pressed(ACTION_MENU)) setOpen(!m_open);

    const float target = m_open ? 1.0f : 0.0f;
    const float step   = fadeSeconds > 0.0f ? dt / fadeSeconds : 1.0f;
    m_fade = m_fade < target ? std::min(target, m_fade + step) : std::max(target, m_fade - step);

    if (UIElement* menu = scene().tryGet<UIElement>(m_menu)) menu->opacity = m_fade;
}

void LabPauseMenu::setOpen(bool open) {
    m_open = open;
    if (!open) showSettings(false);
    scene().get<UICanvas>(m_canvas).visible = open;
    clock().setPaused(open);
}

void LabPauseMenu::showSettings(bool settings) {
    scene().get<UIElement>(m_main).visible     = !settings;
    scene().get<UIElement>(m_settings).visible = settings;
}

EntityId LabPauseMenu::addButton(
    EntityId parent,
    const char* caption,
    const std::string& eventId,
    const glm::vec2& at,
    const glm::vec2& size
) {
    const EntityId id = spawn(caption, parent);
    scene().add(id, UIElement::at({0.5f, 0.0f}, at, size));
    UIButton button;
    button.eventId     = eventId;
    button.normalColor = ACCENT;
    button.hoverColor  = HOT;
    scene().add(id, std::move(button));
    UIText text;
    text.text      = caption;
    text.pixelSize = 22.0f;
    text.color     = WHITE;
    text.align     = UIText::Align::Center;
    text.valign    = UIText::VAlign::Middle;
    scene().add(id, std::move(text));
    return id;
}

EntityId LabPauseMenu::addRow(const char* name, float y) {
    const EntityId row = spawn(name, m_settings);
    scene().add(row, UIElement::at({0.5f, 0.0f}, {0.0f, y}, {ROW_WIDTH, ROW_HEIGHT}));
    UIText text;
    text.text      = name;
    text.pixelSize = 22.0f;
    text.color     = WHITE;
    text.valign    = UIText::VAlign::Middle;
    scene().add(row, std::move(text));
    return row;
}

EntityId LabPauseMenu::addChoice(
    const char* name,
    const std::string& eventId,
    std::vector<std::string> options,
    float y
) {
    const EntityId row = addRow(name, y);

    // The choice is its own text: the option shown between its arrows.
    const EntityId choice = spawn("Choice", row);
    scene().add(choice, UIElement::at({1.0f, 0.5f}, {0.0f, 0.0f}, {240.0f, ROW_HEIGHT}));
    UIText shown;
    shown.pixelSize = 22.0f;
    shown.color     = HOT;
    shown.align     = UIText::Align::Center;
    shown.valign    = UIText::VAlign::Middle;
    scene().add(choice, std::move(shown));

    const auto arrow = [&](const char* caption, const glm::vec2& side) {
        const EntityId id = spawn(caption, choice);
        scene().add(id, UIElement::at(side, {0.0f, 0.0f}, {ROW_HEIGHT, ROW_HEIGHT}));
        UIButton button;
        button.eventId     = eventId;
        button.normalColor = ACCENT;
        button.hoverColor  = HOT;
        scene().add(id, std::move(button));
        UIText text;
        text.text      = caption;
        text.pixelSize = 22.0f;
        text.color     = WHITE;
        text.align     = UIText::Align::Center;
        text.valign    = UIText::VAlign::Middle;
        scene().add(id, std::move(text));
        return id;
    };

    UIChoice steps;
    steps.options = std::move(options);
    steps.back    = arrow("<", {0.0f, 0.5f});
    steps.next    = arrow(">", {1.0f, 0.5f});
    steps.text    = choice;
    scene().add(choice, std::move(steps));
    return choice;
}

EntityId LabPauseMenu::addToggle(const char* name, const std::string& eventId, float y) {
    const EntityId row = addRow(name, y);

    const EntityId toggle = spawn("Toggle", row);
    scene().add(toggle, UIElement::at({1.0f, 0.5f}, {-78.0f, 0.0f}, {84.0f, 36.0f}));
    UIButton button;
    button.eventId            = eventId;
    button.normalColor        = OFF;
    button.hoverColor         = HOT;
    button.shape.cornerRadius = 18.0f;
    scene().add(toggle, std::move(button));

    // The knob slides to the end that says the state; a press on it is a press on the switch.
    const EntityId knob = spawn("Knob", toggle);
    UIElement at = UIElement::at({0.0f, 0.5f}, {0.0f, 0.0f}, {28.0f, 28.0f});
    at.blocksPointer = false;
    scene().add(knob, std::move(at));
    UIImage dot{WHITE, {}, {}};
    dot.shape.cornerRadius = 14.0f;
    scene().add(knob, std::move(dot));

    UIToggle flip;
    flip.onColor = ACCENT;
    flip.knob    = knob;
    scene().add(toggle, std::move(flip));
    return toggle;
}

void LabPauseMenu::showTier(GraphicsPreset preset) {
    const RenderSettings tier = withPreset(projectRender(), preset);
    const auto show = [&](EntityId row, int index) { scene().get<UIChoice>(row).index = index; };
    const auto flip = [&](EntityId row, bool on) { scene().get<UIToggle>(row).on = on; };
    show(m_antiAliasing, nearestIndex(RenderSettings::MSAA_SAMPLE_COUNTS, tier.msaaSamples));
    show(m_shadows,      nearestIndex(RenderSettings::SHADOW_RESOLUTIONS, tier.shadowResolution));
    show(m_renderScale,  nearestIndex(RENDER_SCALES, tier.renderScale));
    flip(m_bloom,        tier.bloom);
    flip(m_occlusion,    tier.gtao);
    flip(m_reflections,  tier.ssr);
    flip(m_softShadows,  tier.softShadows);
    flip(m_culling,      tier.occlusionCulling);
}

void LabPauseMenu::apply() {
    const auto pick = [&](EntityId row, const auto& values) {
        const int index = scene().get<UIChoice>(row).index;
        return values[std::clamp(index, 0, static_cast<int>(std::size(values)) - 1)];
    };
    const auto on = [&](EntityId row) { return scene().get<UIToggle>(row).on; };

    // The project's look is kept: a switch turns off what the project has on, never the reverse.
    // Culling changes no picture, only what the GPU is spared, so it is the player's either way.
    const RenderSettings& project = projectRender();
    const int last    = static_cast<int>(GraphicsPreset::Count) - 1;
    const int quality = std::clamp(scene().get<UIChoice>(m_quality).index, 0, last);

    RenderSettings r   = withPreset(project, static_cast<GraphicsPreset>(quality));
    r.msaaSamples      = pick(m_antiAliasing, RenderSettings::MSAA_SAMPLE_COUNTS);
    r.shadowResolution = pick(m_shadows, RenderSettings::SHADOW_RESOLUTIONS);
    r.renderScale      = pick(m_renderScale, RENDER_SCALES);
    r.bloom            = project.bloom && on(m_bloom);
    r.gtao             = project.gtao && on(m_occlusion);
    r.ssr              = project.ssr && on(m_reflections);
    r.softShadows      = project.softShadows && on(m_softShadows);
    r.occlusionCulling = on(m_culling);
    render()           = r;
}

} // namespace Lab
