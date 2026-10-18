#include "lab_sign.h"

#include <optional>

#include <glm/glm.hpp>

#include "core/clock.h"
#include "core/math/axes.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/scene.h"
#include "system/physics/query/query.h"
#include "system/ui/screen_view.h"

#include "lab_palette.h"

namespace Lab {

namespace {

const glm::vec4 TITLE_INK{1.00f, 0.96f, 0.86f, 1.00f};

constexpr float WIDTH        = 560.0f;
constexpr float TITLE_SIZE   = 34.0f;
constexpr float CAPTION_SIZE = 19.0f;

// How fast the label fades in and out, as a share of whole a second.
constexpr float FADE_RATE = 6.0f;

// Scenery this much short of the label still hides it; what is just behind its point does not.
constexpr float OCCLUSION_SLACK = 0.6f;

// The narrowest band the fade spans, metres: with `fadeFrom` at or past `showWithin` it is a cut.
constexpr float SHORTEST_FADE = 0.001f;

} // namespace

void LabSign::onStart() {
    // A server draws nothing to read.
    if (!window()) return;

    m_canvas = spawn("Sign Label");
    UICanvas canvas;
    canvas.sortOrder = LAYER_SIGNS;
    scene().add(m_canvas, std::move(canvas));

    // Its bottom middle is the point it hangs over; the caption runs under the title.
    m_label = spawn("Label", m_canvas);
    UIElement label;
    label.anchor = {0.0f, 0.0f};
    label.pivot  = {0.5f, 1.0f};
    label.size   = {WIDTH, TITLE_SIZE + CAPTION_SIZE * 3.0f};
    scene().add(m_label, std::move(label));

    addLine("Title", title, TITLE_SIZE, TITLE_INK, 0.0f);
    addLine("Caption", caption, CAPTION_SIZE, SOFT, TITLE_SIZE * 1.2f);
}

EntityId LabSign::addLine(
    const char* name,
    const std::string& text,
    float size,
    const glm::vec4& ink,
    float y
) {
    const EntityId id = spawn(name, m_label);
    scene().add(id, UIElement::at({0.5f, 0.0f}, {0.0f, y}, {WIDTH, size * 2.4f}));
    UIText words;
    words.text      = text;
    words.pixelSize = size;
    words.color     = ink;
    words.shadow    = SHADOW;
    words.align     = UIText::Align::Center;
    words.wrap      = true;
    scene().add(id, std::move(words));
    return id;
}

bool LabSign::isHidden(const glm::vec3& eye, const glm::vec3& point) {
    const glm::vec3 toward   = point - eye;
    const float     distance = glm::length(toward);
    if (distance <= OCCLUSION_SLACK) return false;
    // Static and kinematic bodies - walls, platforms - not the players and props moving about.
    QueryFilter filter;
    filter.hitDynamic = false;
    RayHit hit;
    return raycast(scene(), eye, toward, distance - OCCLUSION_SLACK, hit, filter);
}

void LabSign::onRealtimeUpdate(float) {
    // The pause menu is drawn over the world; a label would show through it.
    if (UICanvas* canvas = scene().tryGet<UICanvas>(m_canvas)) canvas->visible = !clock().isPaused();
}

void LabSign::onUpdate(float dt) {
    UIElement* label = scene().tryGet<UIElement>(m_label);
    const UICanvas* canvas = scene().tryGet<UICanvas>(m_canvas);
    const Transform* at = tryGet<Transform>();
    if (!label || !canvas || !at) return;

    const glm::vec3 point = resolvedWorldPosition(scene(), entity(), *at) + Math::WORLD_UP * height;
    const std::optional<glm::vec2> onCanvas = screen().toCanvas(point, *canvas);
    const Transform* eye = scene().tryGet<Transform>(findActiveCamera(scene()));
    if (!onCanvas || !eye) {
        label->opacity = 0.0f;
        return;
    }

    const float distance = glm::distance(eye->position, point);
    const float span     = glm::max(showWithin - fadeFrom, SHORTEST_FADE);
    float       wanted   = glm::clamp((showWithin - distance) / span, 0.0f, 1.0f);
    if (wanted > 0.0f && isHidden(eye->position, point)) wanted = 0.0f;
    const float step = FADE_RATE * dt;
    m_shown = m_shown < wanted ? glm::min(wanted, m_shown + step) : glm::max(wanted, m_shown - step);
    label->opacity  = m_shown;
    label->position = *onCanvas;
}

} // namespace Lab
