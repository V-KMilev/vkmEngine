#include "ecs/component/ui/ui_build.h"

#include <utility>

#include "ecs/component/scene/name.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_stack.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"

namespace Vkm::Engine::UI {

namespace {

// An element at @p at under @p parent, if there is one.
EntityId element(Scene& scene, EntityId parent, const UIElement& at) {
    const EntityId id = scene.createEntity();
    scene.add(id, at);
    if (parent) HierarchyOperations::setParent(scene, id, parent);
    return id;
}

// One part of a control: a named element pinned at @p anchor that lets presses through to the control.
EntityId part(
    Scene& scene,
    EntityId control,
    const char* name,
    const glm::vec2& anchor,
    const glm::vec2& size
) {
    UIElement at = UIElement::at(anchor, {0.0f, 0.0f}, size);
    at.blocksPointer = false;
    const EntityId id = element(scene, control, at);
    scene.add(id, makeName(name));
    return id;
}

UIText label(std::string said, const Style& style, UIText::Align align, UIText::VAlign valign) {
    UIText text;
    text.text      = std::move(said);
    text.font      = style.font;
    text.pixelSize = style.textSize;
    text.color     = style.text;
    text.align     = align;
    text.valign    = valign;
    text.shadow    = style.shadow;
    return text;
}

UIButton pressable(const Style& style, float rounding) {
    UIButton pressed;
    pressed.normalColor        = style.button;
    pressed.hoverColor         = style.hover;
    pressed.shape.cornerRadius = rounding;
    return pressed;
}

// Left, center or right by where on its parent the element is anchored.
UIText::Align alignOf(const UIElement& at) {
    if (at.anchor.x < 0.5f) return UIText::Align::Left;
    if (at.anchor.x > 0.5f) return UIText::Align::Right;
    return UIText::Align::Center;
}

UIText::VAlign valignOf(const UIElement& at) {
    if (at.anchor.y < 0.5f) return UIText::VAlign::Top;
    if (at.anchor.y > 0.5f) return UIText::VAlign::Bottom;
    return UIText::VAlign::Middle;
}

} // namespace

EntityId canvas(Scene& scene, std::string_view name, int sortOrder) {
    const EntityId id = scene.createEntity();
    scene.add(id, makeName(std::string(name).c_str()));
    UICanvas layer;
    layer.sortOrder = sortOrder;
    scene.add(id, std::move(layer));
    return id;
}

EntityId panel(Scene& scene, EntityId parent, const UIElement& at, const glm::vec4& color) {
    const EntityId id = element(scene, parent, at);
    scene.add(id, UIImage{color, {}, {}});
    return id;
}

EntityId text(Scene& scene, EntityId parent, const UIElement& at, std::string words, const Style& style) {
    const EntityId id = element(scene, parent, at);
    scene.add(id, label(std::move(words), style, alignOf(at), valignOf(at)));
    return id;
}

EntityId column(Scene& scene, EntityId parent, const UIElement& at, float spacing) {
    const EntityId id = element(scene, parent, at);
    scene.add(id, UIStack{UIStack::Axis::Vertical, spacing});
    return id;
}

EntityId row(Scene& scene, EntityId parent, const UIElement& at, float spacing) {
    const EntityId id = element(scene, parent, at);
    scene.add(id, UIStack{UIStack::Axis::Horizontal, spacing});
    return id;
}

EntityId button(Scene& scene, EntityId parent, const UIElement& at, std::string caption, const Style& style) {
    const EntityId id = element(scene, parent, at);
    scene.add(id, pressable(style, style.rounding));
    scene.add(id, label(std::move(caption), style, UIText::Align::Center, UIText::VAlign::Middle));
    return id;
}

EntityId slider(
    Scene& scene,
    EntityId parent,
    const UIElement& at,
    float min,
    float max,
    float value,
    const Style& style
) {
    // The button is the track; a fill grows across it and a knob stands at the value.
    const EntityId track = element(scene, parent, at);
    const float    high  = at.size.y;
    scene.add(track, pressable(style, high * 0.5f));

    const EntityId fill = part(scene, track, "Fill", {0.0f, 0.5f}, {0.0f, 0.0f});
    scene.get<UIElement>(fill).relativeSize = {0.0f, 1.0f};
    UIImage bar{style.accent, {}, {}};
    bar.shape.cornerRadius = high * 0.5f;
    scene.add(fill, std::move(bar));

    const float    across = high * 1.6f;
    const EntityId knob   = part(scene, track, "Knob", {0.5f, 0.5f}, {across, across});
    UIImage dot{style.text, {}, {}};
    dot.shape.cornerRadius = across * 0.5f;
    scene.add(knob, std::move(dot));

    UISlider slide;
    slide.minValue = min;
    slide.maxValue = max;
    slide.value    = value;
    slide.fill     = fill;
    slide.knob     = knob;
    scene.add(track, std::move(slide));
    return track;
}

EntityId toggle(Scene& scene, EntityId parent, const UIElement& at, bool on, const Style& style) {
    // The button is the body; the knob slides to the end that says the state.
    const EntityId body = element(scene, parent, at);
    const float    high = at.size.y;
    scene.add(body, pressable(style, high * 0.5f));

    const float    across = high * 0.75f;
    const EntityId knob   = part(scene, body, "Knob", {0.0f, 0.5f}, {across, across});
    UIImage dot{style.text, {}, {}};
    dot.shape.cornerRadius = across * 0.5f;
    scene.add(knob, std::move(dot));

    UIToggle flip;
    flip.on      = on;
    flip.onColor = style.accent;
    flip.knob    = knob;
    scene.add(body, std::move(flip));
    return body;
}

EntityId choice(
    Scene& scene,
    EntityId parent,
    const UIElement& at,
    std::vector<std::string> options,
    int index,
    const Style& style
) {
    // Its own text shows the option, between two arrows that step it.
    const EntityId shown = element(scene, parent, at);
    UIText option = label({}, style, UIText::Align::Center, UIText::VAlign::Middle);
    option.color = style.hover;
    scene.add(shown, std::move(option));

    const float high = at.size.y;
    const auto arrow = [&](const char* name, const char* caption, const glm::vec2& side) {
        const EntityId id = part(scene, shown, name, side, {high, high});
        scene.get<UIElement>(id).blocksPointer = true;
        scene.add(id, pressable(style, style.rounding));
        scene.add(id, label(caption, style, UIText::Align::Center, UIText::VAlign::Middle));
        return id;
    };

    UIChoice steps;
    steps.options = std::move(options);
    steps.index   = index;
    steps.back    = arrow("Back", "<", {0.0f, 0.5f});
    steps.next    = arrow("Next", ">", {1.0f, 0.5f});
    steps.text    = shown;
    scene.add(shown, std::move(steps));
    return shown;
}

} // namespace Vkm::Engine::UI
