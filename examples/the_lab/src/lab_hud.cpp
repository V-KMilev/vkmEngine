#include "lab_hud.h"

#include <algorithm>
#include <bitset>
#include <limits>

#include "core/clock.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/scene.h"
#include "net/net_session.h"
#include "core/input_map.h"
#include "ecs/component/ui/ui_build.h"

#include "lab_actions.h"
#include "lab_area.h"
#include "lab_palette.h"
#include "lab_seat.h"
#include "lab_state.h"
#include "lab_token.h"

namespace Lab {

namespace {

const glm::vec4 YOU   {1.00f, 0.86f, 0.25f, 1.00f};
const glm::vec4 OTHERS{0.40f, 0.85f, 1.00f, 1.00f};

const glm::vec2 MAP_SIZE{980.0f, 640.0f};
constexpr float MAP_MARGIN = 56.0f;
constexpr float DOT        = 16.0f;

// Seconds a banner takes to fade out at the end of its time.
constexpr float BANNER_FADE = 0.6f;

// An action, and what the controls hint says it does.
struct Control {
    const char* action;
    const char* does;
    bool        offlineOnly;  ///< Named only offline, the one place it does anything
};

// The controls hint after moving, in the order it names them.
const Control CONTROLS[] = {
    {ACTION_JUMP,  "jump",  false},
    {ACTION_WALK,  "walk",  false},
    {ACTION_LOOK,  "look",  false},
    {ACTION_USE,   "use",   false},
    {ACTION_FIRE,  "fire",  false},
    {ACTION_THROW, "throw", false},
    {ACTION_MAP,   "map",   false},
    {ACTION_MENU,  "menu",  true},
};

// Between two controls in the hint.
constexpr const char* CONTROL_GAP = "   ";

// The HUD's look at a text height: white over a shadow, read over any part of the lab.
UI::Style hudText(float pixels) {
    UI::Style style;
    style.textSize = pixels;
    style.text     = WHITE;
    style.shadow   = SHADOW;
    return style;
}

int countOf(uint32_t bits) {
    return static_cast<int>(std::bitset<TOKEN_BITS>(bits).count());
}

// The controls, each named by the key bound to it.
std::string controlsHint(const InputMap& input, bool offline) {
    const std::string move = input.keyName(ACTION_FORWARD) + input.keyName(ACTION_LEFT)
        + input.keyName(ACTION_BACK) + input.keyName(ACTION_RIGHT);
    std::string hint = move + " move";
    for (const Control& control : CONTROLS) {
        const std::string key = input.keyName(control.action);
        if (key.empty() || (control.offlineOnly && !offline)) continue;
        hint += CONTROL_GAP + key + " " + control.does;
    }
    return hint;
}

} // namespace

void LabHud::onStart() {

    forEachBehavior<LabArea>([&](EntityId id, const LabArea& area) {
        const Transform* at = scene().tryGet<Transform>(id);
        if (!at) return;
        const glm::vec3 center = resolvedWorldPosition(scene(), id, *at);
        m_areas.push_back({id, area.title, area.blurb, {center.x, center.z}, area.size});
    });
    std::sort(m_areas.begin(), m_areas.end(), [](const Area& a, const Area& b) {
        return a.entity.slot() < b.entity.slot();
    });
    m_everyToken = 0;
    forEachBehavior<LabToken>([&](EntityId, const LabToken& token) { m_everyToken |= token.bit(); });

    if (!window()) return;

    m_canvas = UI::canvas(scene(), "HUD", LAYER_HUD);

    const EntityId cross = spawn("Crosshair", m_canvas);
    UIElement dot = UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {8.0f, 8.0f});
    dot.blocksPointer = false;
    scene().add(cross, std::move(dot));
    UIImage pip{WHITE, {}, {}};
    pip.shape.cornerRadius = 4.0f;
    pip.shape.borderWidth  = 1.5f;
    pip.shape.borderColor  = SHADOW;
    scene().add(cross, std::move(pip));

    const auto line = [&](const glm::vec2& at, const glm::vec2& offset, const glm::vec2& size, float pixels) {
        return UI::text(scene(), m_canvas, UIElement::at(at, offset, size), "", hudText(pixels));
    };
    m_score       = line({0.0f, 0.0f}, {36.0f, 28.0f}, {700.0f, 200.0f}, 26.0f);
    m_banner      = line({0.5f, 0.0f}, {0.0f, 70.0f}, {1200.0f, 70.0f}, 54.0f);
    m_bannerBlurb = line({0.5f, 0.0f}, {0.0f, 140.0f}, {1100.0f, 40.0f}, 26.0f);
    m_hint        = line({0.5f, 1.0f}, {0.0f, -24.0f}, {1600.0f, 30.0f}, 20.0f);
    scene().get<UIText>(m_bannerBlurb).color = SOFT;
    scene().get<UIText>(m_hint).color        = SOFT;
    scene().get<UIText>(m_hint).text         = controlsHint(input(), net().isOffline());

    buildMap();
}

glm::vec2 LabHud::onMap(const glm::vec2& world) const {
    return m_mapOrigin + (world - m_worldMin) * m_mapScale;
}

void LabHud::buildMap() {
    m_map = spawn("Map", m_canvas);

    UIElement panel = UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, MAP_SIZE);
    panel.visible       = false;
    panel.blocksPointer = false;
    scene().add(m_map, std::move(panel));
    UIImage back{INK, {}, {}};
    back.shape.cornerRadius = 18.0f;
    scene().add(m_map, std::move(back));

    const UIElement top = UIElement::at({0.5f, 0.0f}, {0.0f, 14.0f}, {MAP_SIZE.x, 40.0f});
    const std::string closes = "THE LAB  -  " + input().keyName(ACTION_MAP) + " closes the map";
    UI::text(scene(), m_map, top, closes, hudText(30.0f));
    if (m_areas.empty()) return;

    // The areas' bounds, fitted into the panel below its title; north is up, so -Z is up.
    glm::vec2 lo{std::numeric_limits<float>::max()};
    glm::vec2 hi{std::numeric_limits<float>::lowest()};
    for (const Area& area : m_areas) {
        lo = glm::min(lo, area.center - area.size * 0.5f);
        hi = glm::max(hi, area.center + area.size * 0.5f);
    }
    const glm::vec2 room   = MAP_SIZE - glm::vec2(2.0f * MAP_MARGIN, 2.0f * MAP_MARGIN + 30.0f);
    const glm::vec2 extent = glm::max(hi - lo, glm::vec2(1.0f));
    m_mapScale  = std::min(room.x / extent.x, room.y / extent.y);
    m_worldMin  = lo;
    m_mapOrigin = (MAP_SIZE - extent * m_mapScale) * 0.5f + glm::vec2(0.0f, 20.0f);

    for (const Area& area : m_areas) {
        const LabArea* source = findBehavior<LabArea>(area.entity);
        const EntityId patch = spawn("Area", m_map);
        const glm::vec2 size = area.size * m_mapScale - glm::vec2(6.0f);
        UIElement rect = UIElement::at({0.0f, 0.0f}, onMap(area.center) - size * 0.5f, size);
        rect.blocksPointer = false;
        scene().add(patch, std::move(rect));
        UIImage fill{source ? source->color : SOFT, {}, {}};
        fill.shape.cornerRadius = 10.0f;
        scene().add(patch, std::move(fill));

        const UIElement middle = UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, size);
        const EntityId  name   = UI::text(scene(), patch, middle, area.title, hudText(22.0f));
        scene().get<UIText>(name).wrap = true;
    }

    for (const EntityId character : authoredCharacters(scene())) {
        const EntityId dot = spawn("Player", m_map);
        UIElement at = UIElement::at({0.0f, 0.0f}, {0.0f, 0.0f}, {DOT, DOT});
        at.blocksPointer = false;
        scene().add(dot, std::move(at));
        UIImage disc{OTHERS, {}, {}};
        disc.shape.cornerRadius = DOT * 0.5f;
        disc.shape.borderWidth  = 2.0f;
        disc.shape.borderColor  = SHADOW;
        scene().add(dot, std::move(disc));
        m_pins.push_back({character, dot});
    }
}

void LabHud::updateMap() {
    const EntityId you = localCharacter(scene(), net());
    for (const Pin& pin : m_pins) {
        UIElement* dot = scene().tryGet<UIElement>(pin.dot);
        const Transform* body = scene().tryGet<Transform>(pin.character);
        if (!dot) continue;
        // Offline only the seat is played, and the others leave the world. Online a client is not told
        // which seats are taken, so every character shows, a free seat where it was authored.
        dot->visible = body && (!net().isOffline() || pin.character == you);
        if (!body) continue;
        dot->position = onMap({body->position.x, body->position.z}) - glm::vec2(DOT * 0.5f);
        if (UIImage* disc = scene().tryGet<UIImage>(pin.dot)) {
            disc->color = pin.character == you ? YOU : OTHERS;
        }
    }
}

void LabHud::showBanner(const std::string& title, const std::string& blurb) {
    scene().get<UIText>(m_banner).text      = title;
    scene().get<UIText>(m_bannerBlurb).text = blurb;
    m_bannerLeft = bannerSeconds;
}

void LabHud::nameArea(const glm::vec2& feet) {
    int inside = -1;
    for (size_t i = 0; i < m_areas.size(); ++i) {
        const glm::vec2 offset = glm::abs(feet - m_areas[i].center);
        if (offset.x <= m_areas[i].size.x * 0.5f && offset.y <= m_areas[i].size.y * 0.5f) {
            inside = static_cast<int>(i);
            break;
        }
    }
    if (inside == m_inArea) return;
    m_inArea = inside;
    if (inside < 0) return;
    const Area& area = m_areas[static_cast<size_t>(inside)];
    showBanner(area.title, area.blurb);
}

void LabHud::cheerTokens(uint32_t tokens) {
    if (!m_tokens.rose(tokens)) return;
    const int found = countOf(tokens & m_everyToken);
    const int every = countOf(m_everyToken);
    if (found >= every) {
        showBanner("EVERY TOKEN FOUND", "The lab is yours.");
        return;
    }
    showBanner(
        "TOKEN " + std::to_string(found) + " OF " + std::to_string(every),
        "Tokens hide all over the lab: up high, down low, and somewhere dark."
    );
}

void LabHud::onUpdate(float dt) {
    if (!m_canvas) return;
    const EntityId you = localCharacter(scene(), net());

    if (pressed(ACTION_MAP)) {
        if (UIElement* map = scene().tryGet<UIElement>(m_map)) map->visible = !map->visible;
    }
    const UIElement* map = scene().tryGet<UIElement>(m_map);
    if (map && map->visible) updateMap();

    const LabScore* score = scene().tryGet<LabScore>(you);
    if (const Transform* body = scene().tryGet<Transform>(you)) {
        nameArea({body->position.x, body->position.z});
    }
    if (score) cheerTokens(score->tokens);

    // Seen, not just shown: the banner for where the player starts would fade under the cover.
    if (!isCovered()) m_bannerLeft = std::max(0.0f, m_bannerLeft - dt);
    const float shown = std::min(1.0f, m_bannerLeft / BANNER_FADE);
    scene().get<UIElement>(m_banner).opacity      = shown;
    scene().get<UIElement>(m_bannerBlurb).opacity = shown;

    std::string text;
    if (m_everyToken) {
        text = "TOKENS  " + std::to_string(countOf(score ? score->tokens & m_everyToken : 0u)) + " / "
            + std::to_string(countOf(m_everyToken));
    }
    scene().get<UIText>(m_score).text = text;
}

void LabHud::onRealtimeUpdate(float) {
    const bool frozen = clock().isPaused() || clock().isMenuShown();
    if (UICanvas* canvas = scene().tryGet<UICanvas>(m_canvas)) canvas->visible = !frozen;
}

} // namespace Lab
