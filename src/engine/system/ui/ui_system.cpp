#include "system/ui/ui_system.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#include <glm/gtc/constants.hpp>

#include "ecs/scene.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_rect.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_stack.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "core/frame/loading_frame.h"
#include "core/frame/loading_bar.h"
#include "core/clock.h"
#include "core/graphics.h"
#include "core/host_chrome.h"
#include "base/event/event_bus.h"
#include "system/ui/text_layout.h"
#include "core/frame/ui_events.h"
#include "vkm_platform/window/window_manager.h"
#include "core/input_map.h"
#include "net/net_session.h"
#include "base/profile/profiler.h"

namespace Vkm::Engine {

namespace {

// The status line above the loading bar: reference pixels high, its gap to the bar, and its alpha.
constexpr float COVER_TEXT_SIZE  = 22.0f;
constexpr float COVER_TEXT_GAP   = 14.0f;
constexpr float COVER_TEXT_ALPHA = 0.7f;
// A project's tip under the bar: reference pixels high, its gap to the bar, the share of the
// screen's width it wraps at, and its alpha and its shadow's.
constexpr float COVER_TIP_SIZE   = 24.0f;
constexpr float COVER_TIP_GAP    = 26.0f;
constexpr float COVER_TIP_WIDTH  = 0.5f;
constexpr float COVER_TIP_ALPHA  = 0.85f;
constexpr float COVER_TIP_SHADOW = 0.6f;

// Float equality is right: a clip is copied down the walk, not recomputed, so runs
// under one panel hold bit-identical rects.
bool sameClip(const UIRect& a, const UIRect& b) {
    return a.pos == b.pos && a.size == b.size;
}

// What a subtree that drew nothing reports: below every corner, so it moves no max.
glm::vec2 nothingDrawn() {
    return glm::vec2(std::numeric_limits<float>::lowest());
}

// @p color with its alpha scaled by @p opacity.
glm::vec4 faded(glm::vec4 color, float opacity) {
    color.a *= opacity;
    return color;
}

const glm::vec4& fadeEnd(const glm::vec4& color, const UIShape& shape) {
    return shape.gradient ? shape.bottomColor : color;
}

// A button's tint: a toggle that is on rests in its own color.
const glm::vec4& tintOf(const Scene& scene, EntityId entity, const UIButton& button) {
    const UIToggle* toggle = scene.tryGet<UIToggle>(entity);
    const bool resting = button.state == UIButton::State::Normal;
    const bool onRest  = toggle && toggle->on && toggle->onColor.a > 0.0f && resting;
    return onRest ? toggle->onColor : button.colorForState();
}

// A slider's fill and knob, a toggle's knob and a choice's text, put where the value says before
// the walk lays them out.
void placeParts(Scene& scene, EntityId entity) {
    if (const UISlider* slider = scene.tryGet<UISlider>(entity)) {
        const float along = slider->fraction();
        if (UIElement* fill = scene.tryGet<UIElement>(slider->fill)) fill->relativeSize.x = along;
        if (UIElement* knob = scene.tryGet<UIElement>(slider->knob)) knob->anchor.x = along;
    }
    if (const UIToggle* toggle = scene.tryGet<UIToggle>(entity)) {
        if (UIElement* knob = scene.tryGet<UIElement>(toggle->knob)) {
            knob->anchor.x = toggle->on ? 1.0f : 0.0f;
            knob->pivot.x  = knob->anchor.x;
        }
    }
    if (const UIChoice* choice = scene.tryGet<UIChoice>(entity)) {
        if (UIText* text = scene.tryGet<UIText>(choice->text)) {
            const size_t index = static_cast<size_t>(choice->index);
            if (choice->index < 0 || index >= choice->options.size()) text->text.clear();
            else if (text->text != choice->options[index]) text->text = choice->options[index];
        }
    }
}

// Step the choice whose `back` or `next` @p button is, and say so on the bus; false for a button
// that steps none. Every choice is looked at, which a click can afford.
bool stepChoice(Scene& scene, EntityId button, EventBus& events) {
    bool stepped = false;
    scene.forEach<UIChoice>([&](EntityId entity, UIChoice& choice) {
        const int by = button == choice.next ? 1 : (button == choice.back ? -1 : 0);
        if (by == 0 || choice.options.empty()) return;
        // An index out of range steps as though from just past the end the arrow leaves; at the
        // end it points to, a step stays, and says nothing.
        const int count = static_cast<int>(choice.options.size());
        const bool inRange = choice.index >= 0 && choice.index < count;
        const int from = inRange ? choice.index : (by > 0 ? -1 : count);
        const int to   = std::clamp(from + by, 0, count - 1);
        stepped = true;
        if (to == choice.index) return;
        choice.index = to;
        events.enqueue(UIValueEvent{entity, static_cast<float>(choice.index)});
    });
    return stepped;
}

// A held slider's value, from where the pointer is across its button; true when it moved.
bool dragSlider(UISlider& slider, const UIButton& button) {
    if (!(slider.maxValue > slider.minValue) || button.resolvedSize.x <= 0.0f) return false;
    const float along = glm::clamp(button.pointer.x / button.resolvedSize.x, 0.0f, 1.0f);
    float value = slider.minValue + along * (slider.maxValue - slider.minValue);
    if (slider.step > 0.0f) {
        value = slider.minValue + std::round((value - slider.minValue) / slider.step) * slider.step;
        value = std::min(value, slider.maxValue);
    }
    if (value == slider.value) return false;
    slider.value = value;
    return true;
}

} // namespace

UISystem::UISystem() {
    // The default font, white, shadowed: read over any frame.
    m_frameRate.pixelSize = FRAME_RATE_SIZE;
    m_frameRate.shadow    = glm::vec4(0.0f, 0.0f, 0.0f, 0.85f);
    m_frameRate.align     = UIText::Align::Right;
}

void UISystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("UISystem");

    m_drawData.clear();
    clearHits();
    ctx.ui = &m_drawData;
    ctx.input.setPointerBlockers(m_blockingAreas);

    holdForMenus(ctx);

    // Rects are in framebuffer pixels, the cursor in window pixels; they differ on a
    // scaled display.
    const float pointerScale = ctx.window.framebufferScale();
    const HostChrome::ViewportRect vp = ctx.chrome.viewportIn(ctx.window);
    m_pointerScale   = pointerScale > 0.0f ? pointerScale : 1.0f;
    m_viewportOrigin = {static_cast<float>(vp.x), static_cast<float>(vp.y)};
    m_pointer = ctx.input.pointer() * pointerScale
        - glm::vec2(static_cast<float>(vp.x), static_cast<float>(vp.y));

    // A disabled cursor is a look control whose virtual position points at nothing,
    // so a HUD it crosses must not take the click.
    m_cursorFree = ctx.window.cursorMode() != Platform::CursorMode::Disabled || ctx.window.cursorFree();

    // The loading screen over everything takes the pointer: nothing under it is hovered,
    // pressed or scrolled.
    const bool covered = ctx.loading && ctx.loading->isShowing();

    // Resolved whoever owns the pointer, so a host can still ask pointerTarget. The
    // map already hides buttons and wheel under the chrome, so only hover asks it.
    m_pointerIsOurs = m_cursorFree && !ctx.chrome.capturePointer && !covered;

    // The chrome or the cover taking the pointer disarms the press: kept armed, a press
    // let go over a panel could become a click on a later release.
    if (!m_pointerIsOurs) m_pressedButton = {};

    const float vpW = static_cast<float>(vp.width);
    const float vpH = static_cast<float>(vp.height);
    if (vpW <= 0.0f || vpH <= 0.0f) return;

    const UIRect viewport{glm::vec2(0.0f), glm::vec2(vpW, vpH)};

    m_coverData.clear();
    if (covered) {
        layOutCover(ctx, viewport);
        ctx.cover = &m_coverData;
    }

    const bool readsFrameRate = showsFrameRate(ctx);
    const Walk world{ctx.scene, m_drawData};
    if (readsFrameRate) refreshFrameRate(ctx);
    layOutCanvases(ctx, world, viewport);
    if (readsFrameRate) appendFrameRate(ctx, viewport);

    // A toggle flipped, a slider dragged or a choice stepped has moved its own parts, which the
    // walk placed by the value before: laid out again, so they show the change the frame it happens.
    if (resolveInteraction(ctx, covered)) {
        const EntityId target = m_drawData.pointerTarget;
        m_drawData.clear();
        clearHits();
        layOutCanvases(ctx, world, viewport);
        if (readsFrameRate) appendFrameRate(ctx, viewport);
        m_drawData.pointerTarget = target;
    }
    if (covered) {
        m_blockingAreas.emplace_back(m_viewportOrigin / m_pointerScale, viewport.size / m_pointerScale);
    }
    ctx.input.setPointerBlockers(m_blockingAreas);
}

void UISystem::holdForMenus(FrameContext& ctx) {
    const bool running = !ctx.clock.isPaused();
    bool menu = false;
    ctx.scene.forEach<UICanvas>([&](EntityId, UICanvas& canvas) {
        if (running && !canvas.toggleAction.empty() && ctx.input.pressed(canvas.toggleAction)) {
            canvas.visible = !canvas.visible;
        }
        menu = menu || (canvas.visible && canvas.pausesGame);
    });
    menu = menu && running;
    ctx.clock.setMenuShown(menu && ctx.net.isOffline());
    ctx.window.setCursorFree(menu);
}

void UISystem::layOutCover(FrameContext& ctx, const UIRect& viewport) {
    const LoadingFrame& loading = *ctx.loading;
    const float shown = loading.opacity * loading.content;
    const float scale = viewport.size.y / LOADING_REFERENCE_HEIGHT;

    // The project's own screen first, under the line and the tip; a picture, which nothing presses.
    if (loading.screen && loading.screenAssets == ctx.resources.epoch()) {
        layOutCanvases(ctx, Walk{*loading.screen, m_coverData}, viewport, shown);
        clearHits();
    }

    // The bar is the backend's, drawn over this list (the Loading pass); the line sits above it.
    const LoadingBarRect placed = loadingBar(viewport.size.x, viewport.size.y);
    const glm::vec2      bar(placed.width, placed.height);
    const glm::vec2      at(std::round(viewport.pos.x) + placed.x, std::round(viewport.pos.y) + placed.y);

    UIText line;
    const float gap = COVER_TEXT_GAP * scale;

    line.text      = loading.status;
    line.pixelSize = COVER_TEXT_SIZE;
    line.color     = glm::vec4(1.0f, 1.0f, 1.0f, COVER_TEXT_ALPHA);
    line.shadow    = glm::vec4(0.0f);
    line.align     = UIText::Align::Center;
    line.valign    = UIText::VAlign::Bottom;
    const UIRect above{glm::vec2(at.x, viewport.pos.y), glm::vec2(bar.x, at.y - gap - viewport.pos.y)};
    emitTextIn(ctx, m_coverData, line, above, viewport, scale, shown);

    // The tip under the bar, wrapped to a column.
    if (!loading.tip.empty()) {
        UIText tip;
        tip.text      = loading.tip;
        tip.pixelSize = COVER_TIP_SIZE;
        tip.color     = glm::vec4(1.0f, 1.0f, 1.0f, COVER_TIP_ALPHA);
        tip.shadow    = glm::vec4(0.0f, 0.0f, 0.0f, COVER_TIP_SHADOW);
        tip.align     = UIText::Align::Center;
        tip.wrap      = true;
        const float wide = std::round(viewport.size.x * COVER_TIP_WIDTH);
        const float top  = at.y + bar.y + COVER_TIP_GAP * scale;
        const glm::vec2 from(std::round(viewport.pos.x + (viewport.size.x - wide) * 0.5f), top);
        const UIRect    below{from, glm::vec2(wide, viewport.pos.y + viewport.size.y - top)};
        emitTextIn(ctx, m_coverData, tip, below, viewport, scale, shown * loading.tipShown);
    }
}

void UISystem::clearHits() {
    m_buttonHits.clear();
    m_pointerBlockers.clear();
    m_blockingAreas.clear();
    m_scrollTargets.clear();
}

bool UISystem::showsFrameRate(const FrameContext& ctx) {
    // A host authoring the world shows its own.
    return ctx.graphics.settings().showFrameRate && !ctx.chrome.authoring;
}

void UISystem::refreshFrameRate(const FrameContext& ctx) {
    // Read off the smoothed clock a few times a second, so the digits hold still long enough to read.
    m_frameRateAge += ctx.clock.getDeltaTime();
    if (!m_frameRate.text.empty() && m_frameRateAge < FRAME_RATE_REFRESH) return;
    m_frameRateAge = 0.0f;
    char line[48];
    const double fps = ctx.clock.getFrameRate();
    const double ms  = ctx.clock.getFrameTime();
    std::snprintf(line, sizeof(line), "%.0f FPS  %.1f ms", fps, ms);
    m_frameRate.text = line;
}

void UISystem::appendFrameRate(FrameContext& ctx, const UIRect& viewport) {
    // Sized as a canvas at the reference height would size it, inset from the top-right corner.
    const float     scale = viewport.size.y / UICanvas::REFERENCE_HEIGHT;
    const glm::vec2 inset = glm::vec2(FRAME_RATE_INSET * scale);
    const UIRect    rect{inset, viewport.size - 2.0f * inset};
    emitTextIn(ctx, m_drawData, m_frameRate, rect, viewport, scale, 1.0f);
}

void UISystem::layOutCanvases(FrameContext& ctx, const Walk& walk, const UIRect& viewport, float opacity) {
    // SparseSet order is arbitrary; the entity index breaks ties for stability.
    m_canvases.clear();
    walk.scene.forEach<UICanvas>([&](EntityId entity, const UICanvas& canvas) {
        if (canvas.visible) m_canvases.push_back(CanvasRef{canvas.sortOrder, entity});
    });
    std::sort(
        m_canvases.begin(),
        m_canvases.end(),
        [](const CanvasRef& a, const CanvasRef& b) {
            if (a.sortOrder != b.sortOrder) return a.sortOrder < b.sortOrder;
            return a.entity.slot() < b.entity.slot();
        }
    );

    for (const CanvasRef& ref : m_canvases) {
        const UICanvas& canvas = walk.scene.get<UICanvas>(ref.entity);
        const float scale = UICanvas::scaleIn(canvas, viewport.size.y);

        // The viewport is both the top-level parent rect and the starting clip.
        HierarchyOperations::forEachChild(walk.scene, ref.entity, [&](EntityId child) {
            resolveElement(ctx, walk, child, viewport, viewport, scale, opacity, 0);
        });
    }
}

glm::vec2 UISystem::resolveElement(
    FrameContext& ctx,
    const Walk& walk,
    EntityId entity,
    const UIRect& parentRect,
    const UIRect& clip,
    float scale,
    float inherited,
    uint32_t depth
) {
    if (depth >= HierarchyOperations::MAX_DEPTH) {
        HierarchyOperations::warnWalkBound("UI layout", HierarchyOperations::MAX_DEPTH);
        return nothingDrawn();
    }
    // A canvas inside another's tree is laid out as its own, once, not as an element too.
    Scene& scene    = walk.scene;
    UIElement* held = scene.tryGet<UIElement>(entity);
    if (!held || scene.has<UICanvas>(entity)) return nothingDrawn();

    UIElement& element = *held;
    if (!element.visible) return nothingDrawn();
    const float opacity = inherited * glm::clamp(element.opacity, 0.0f, 1.0f);
    if (opacity <= glm::epsilon<float>()) return nothingDrawn();

    const glm::vec2 sizePx = glm::max(
        element.size * scale + element.relativeSize * parentRect.size,
        glm::vec2(0.0f)
    );
    const glm::vec2 anchorPx = parentRect.pos + element.anchor * parentRect.size;
    element.screenRect = UIRect{anchorPx + element.position * scale - element.pivot * sizePx, sizePx};

    // Clipped away is out of reach: a row scrolled out of sight takes no click.
    const bool pointerHere = m_cursorFree && clip.intersected(element.screenRect).contains(m_pointer);

    // Before anything draws, so a choice that is its own text shows the option it holds now.
    placeParts(scene, entity);

    const bool drewImage    = emitImage(walk, entity, element, clip, scale, opacity);
    const bool drewButton   = emitButton(walk, entity, element, clip, scale, opacity);
    const glm::vec2 textMax = emitText(ctx, walk, entity, element, clip, scale, opacity);

    // Only what draws can block; text over a button is a caption on it.
    const bool draws = drewImage || drewButton;
    if (draws && element.blocksPointer && pointerHere) m_pointerBlockers.push_back(entity);
    // Wherever the pointer is: the next frame's input asks it of the pointer then.
    if (draws && element.blocksPointer && m_cursorFree) {
        const UIRect seen = clip.intersected(element.screenRect);
        if (seen.size.x > 0.0f && seen.size.y > 0.0f) {
            const glm::vec2 at = (seen.pos + m_viewportOrigin) / m_pointerScale;
            m_blockingAreas.emplace_back(at, seen.size / m_pointerScale);
        }
    }

    UIScroll* scroll = scene.tryGet<UIScroll>(entity);
    const bool clips = scroll != nullptr || element.clipChildren;
    const UIRect childClip = clips ? clip.intersected(element.screenRect) : clip;
    UIRect childRect = element.screenRect;

    if (scroll) {
        // Laid out at the offset clamped to last frame's content; the stored offset is
        // clamped below, or one set the frame a row was added would stop short for good.
        scroll->viewSize = element.screenRect.size / scale;
        childRect.pos   -= glm::clamp(scroll->offset, glm::vec2(0.0f), scroll->range()) * scale;

        // A scroll view always clips, so its window is the rect pointerHere tested.
        if (pointerHere) m_scrollTargets.push_back(ScrollTarget{entity, m_pointerBlockers.size()});
    }

    // A stack gives each shown child a slot of its own size along its axis, one after another.
    const UIStack* stack = scene.tryGet<UIStack>(entity);
    const int along = stack && stack->axis == UIStack::Axis::Horizontal ? 0 : 1;
    float taken = 0.0f;

    glm::vec2 contentMax = nothingDrawn();
    HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
        UIRect slot = childRect;
        if (stack) {
            // As resolveElement skips them: hidden, faded out, or a canvas of its own.
            const UIElement* placed = scene.tryGet<UIElement>(child);
            if (!placed || !placed->visible || scene.has<UICanvas>(child)) return;
            if (placed->opacity <= glm::epsilon<float>()) return;
            const float extent = std::max(
                placed->size[along] * scale + placed->relativeSize[along] * childRect.size[along],
                0.0f
            );
            slot.pos[along] += taken;
            slot.size[along] = extent;
            taken += extent + stack->spacing * scale;
        }
        const glm::vec2 drawn = resolveElement(ctx, walk, child, slot, childClip, scale, opacity, depth + 1);
        contentMax = glm::max(contentMax, drawn);
    });

    if (scroll) {
        scroll->contentSize = glm::max(contentMax - childRect.pos, glm::vec2(0.0f)) / scale;
        scroll->offset      = glm::clamp(scroll->offset, glm::vec2(0.0f), scroll->range());
    }

    if (clips) return element.screenRect.max();

    const glm::vec2 drawn = draws ? element.screenRect.max() : nothingDrawn();
    return glm::max(glm::max(drawn, textMax), contentMax);
}

void UISystem::appendCommand(
    UIDrawData& out,
    uint32_t first,
    uint32_t count,
    FontHandle font,
    TextureHandle image,
    const UIRect& clip
) {
    if (!out.commands.empty()) {
        UIDrawCmd& last = out.commands.back();
        const bool sameAtlas = !font || !last.font || last.font == font;
        const bool sameImage = !image || !last.image || last.image == image;
        if (sameAtlas && sameImage && sameClip(last.clip, clip)
            && last.firstQuad + last.quadCount == first) {
            last.quadCount += count;
            if (font)  last.font  = font;
            if (image) last.image = image;
            return;
        }
    }
    out.commands.push_back(UIDrawCmd{first, count, clip, font, image});
}

bool UISystem::emitImage(
    const Walk& walk,
    EntityId entity,
    const UIElement& element,
    const UIRect& clip,
    float canvasScale,
    float opacity
) {
    const UIImage* image = walk.scene.tryGet<UIImage>(entity);
    if (!image) return false;

    const uint32_t first = static_cast<uint32_t>(walk.out.quads.size());
    const bool textured = static_cast<bool>(image->texture);
    UIShape shape = image->shape;
    shape.bottomColor.a *= opacity;
    shape.borderColor.a *= opacity;
    appendShaped(walk.out, element.screenRect, faded(image->color, opacity), shape, canvasScale, textured);
    appendCommand(walk.out, first, 1, {}, image->texture, clip);
    return true;
}

bool UISystem::emitButton(
    const Walk& walk,
    EntityId entity,
    const UIElement& element,
    const UIRect& clip,
    float canvasScale,
    float opacity
) {
    UIButton* button = walk.scene.tryGet<UIButton>(entity);
    if (!button) return false;

    // In the element's authored space, free of canvas scale and viewport offset.
    button->pointer      = (m_pointer - element.screenRect.pos) / canvasScale;
    button->resolvedSize = element.screenRect.size / canvasScale;

    const uint32_t first = static_cast<uint32_t>(walk.out.quads.size());
    UIShape shape = button->shape;
    shape.bottomColor.a *= opacity;
    shape.borderColor.a *= opacity;
    const glm::vec4 tint = faded(tintOf(walk.scene, entity, *button), opacity);
    appendShaped(walk.out, element.screenRect, tint, shape, canvasScale, false);
    appendCommand(walk.out, first, 1, {}, {}, clip);

    m_buttonHits.push_back(ButtonHit{entity, first, opacity});
    return true;
}

bool UISystem::resolveInteraction(FrameContext& ctx, bool covered) {
    const EntityId topmost = m_pointerBlockers.empty() ? EntityId{} : m_pointerBlockers.back();
    bool moved = false;

    // Every wheel notch: gameplay's wheel() reads zero over a blocker, so they come here.
    const bool down     = ctx.input.held(InputActions::UI_CLICK);
    const bool pressed  = !covered && ctx.input.pressed(InputActions::UI_CLICK);
    const bool released = ctx.input.released(InputActions::UI_CLICK);
    const float wheel   = covered ? 0.0f : ctx.input.uiWheel();

    m_drawData.pointerTarget = topmost;

    // Both edges in one frame are a tap - pressed then let go - or, held at its end, a release and
    // a press again (a quick second click): the release is the press before's, then the new one arms.
    const bool restruck = pressed && released && down;
    EntityId letGo = released && restruck ? m_pressedButton : EntityId{};
    if (pressed) {
        // has() answers false for the null `topmost`, so no separate check.
        const bool onAButton = ctx.scene.has<UIButton>(topmost);
        m_pressedButton = onAButton ? topmost : EntityId{};
    }
    if (released && !restruck) letGo = m_pressedButton;

    for (const ButtonHit& hit : m_buttonHits) {
        UIButton& button = ctx.scene.get<UIButton>(hit.entity);

        // The topmost blocker is under the pointer by construction.
        const bool isTopmost = button.interactable && m_pointerIsOurs && hit.entity == topmost;
        button.held = button.interactable && down && m_pressedButton == hit.entity;
        const bool held = isTopmost && button.held;

        if (released && isTopmost && letGo == hit.entity) {
            if (UIToggle* toggle = ctx.scene.tryGet<UIToggle>(hit.entity)) {
                toggle->on   = !toggle->on;
                moved        = true;
                ctx.events.enqueue(UIValueEvent{hit.entity, toggle->on ? 1.0f : 0.0f});
            }
            moved |= stepChoice(ctx.scene, hit.entity, ctx.events);
            ctx.events.enqueue(UIClickEvent{hit.entity});
        }

        // Followed wherever the pointer goes while the press lasts, as a scrollbar is.
        UISlider* slider = button.held ? ctx.scene.tryGet<UISlider>(hit.entity) : nullptr;
        if (slider && dragSlider(*slider, button)) {
            moved = true;
            ctx.events.enqueue(UIValueEvent{hit.entity, slider->value});
        }

        if (!button.interactable) button.state = UIButton::State::Disabled;
        else if (held)            button.state = UIButton::State::Pressed;
        else if (isTopmost)       button.state = UIButton::State::Hover;
        else                      button.state = UIButton::State::Normal;

        const glm::vec4& tint = tintOf(ctx.scene, hit.entity, button);
        UIQuad& quad = m_drawData.quads[hit.quad];
        quad.top    = faded(tint, hit.opacity);
        quad.bottom = faded(fadeEnd(tint, button.shape), hit.opacity);
    }

    if (released && !restruck) m_pressedButton = {};

    // The innermost window entered after the topmost blocker lies in or over it, so
    // takes the wheel. Failing one, the wheel goes to the nearest scroll view holding
    // the blocker, so a panel over a list takes the wheel as well as the click.
    if (wheel != 0.0f) {
        EntityId target;
        for (auto it = m_scrollTargets.rbegin(); it != m_scrollTargets.rend(); ++it) {
            if (it->blockersBefore < m_pointerBlockers.size()) continue;
            target = it->entity;
            break;
        }
        if (!target && topmost) {
            target = HierarchyOperations::findInSelfOrAncestors<UIScroll>(ctx.scene, topmost);
        }

        if (UIScroll* scroll = ctx.scene.tryGet<UIScroll>(target)) {
            const glm::vec2 room = scroll->range();
            // One wheel drives whichever axis overflows, vertical first.
            const int axis = room.y > 0.0f ? 1 : 0;
            scroll->offset[axis] = glm::clamp(
                scroll->offset[axis] - wheel * scroll->wheelStep,
                0.0f,
                room[axis]
            );
        }
    }
    return moved;
}

void UISystem::breakLines(
    std::string_view text,
    const FontAsset& font,
    float scale,
    float maxWidth,
    bool wrap
) {
    m_lines.clear();

    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t hardEnd = text.find('\n', lineStart);
        if (hardEnd == std::string_view::npos) hardEnd = text.size();

        if (!wrap) {
            m_lines.push_back(text.substr(lineStart, hardEnd - lineStart));
        } else {
            size_t cursor = lineStart;
            while (cursor < hardEnd) {
                const std::string_view rest = text.substr(cursor, hardEnd - cursor);
                size_t overflow  = std::string_view::npos;
                size_t lastSpace = std::string_view::npos;
                // A space never overflows a line: it hangs past the edge and the break eats it.
                walkGlyphs(font, rest, scale, [&](size_t at, const FontGlyph& glyph, float pen) {
                    if (rest[at] == ' ') {
                        lastSpace = at;
                        return true;
                    }
                    if (at > 0 && pen + glyph.advance * scale > maxWidth) {
                        overflow = at;
                        return false;
                    }
                    return true;
                });
                if (overflow == std::string_view::npos) {
                    m_lines.push_back(rest);
                    cursor = hardEnd;
                    break;
                }
                const size_t breakAt = (lastSpace != std::string_view::npos) ? lastSpace : overflow;
                m_lines.push_back(rest.substr(0, breakAt));
                // Skip the space that ended the line; a mid-word break has none.
                cursor += (lastSpace != std::string_view::npos) ? breakAt + 1 : breakAt;
            }
            if (cursor == lineStart && lineStart == hardEnd) m_lines.emplace_back();
        }

        if (hardEnd == text.size()) break;
        lineStart = hardEnd + 1;
        if (lineStart == text.size()) {
            m_lines.emplace_back();
            break;
        }
    }
}

glm::vec2 UISystem::emitText(
    FrameContext& ctx,
    const Walk& walk,
    EntityId entity,
    const UIElement& element,
    const UIRect& clip,
    float canvasScale,
    float opacity
) {
    const UIText* held = walk.scene.tryGet<UIText>(entity);
    if (!held) return nothingDrawn();
    return emitTextIn(ctx, walk.out, *held, element.screenRect, clip, canvasScale, opacity);
}

glm::vec2 UISystem::emitTextIn(
    FrameContext& ctx,
    UIDrawData& out,
    const UIText& text,
    const UIRect& rect,
    const UIRect& clip,
    float canvasScale,
    float opacity
) {
    const glm::vec2 nothing = nothingDrawn();
    if (text.text.empty() || text.font.empty()) return nothing;

    // By name, the serializable identity, so the component stays plain data.
    const FontHandle fontHandle = ctx.resources.findByName<FontAsset>(text.font);
    if (!fontHandle) return nothing;
    const FontAsset& font = ctx.resources.get(fontHandle);
    if (font.pixelHeight <= 0.0f) return nothing;

    const float renderScale = text.pixelSize * canvasScale / font.pixelHeight;

    breakLines(text.text, font, renderScale, rect.size.x, text.wrap);
    if (m_lines.empty()) return nothing;

    // The block spans ascent above the first baseline to descent (negative) below the
    // last; vertical alignment offsets it by the unused height.
    const float lineStep    = font.lineHeight * renderScale;
    const float blockHeight = (font.ascent - font.descent) * renderScale
        + static_cast<float>(m_lines.size() - 1) * lineStep;

    float firstBaseline = rect.pos.y + font.ascent * renderScale;
    if (text.valign == UIText::VAlign::Middle) {
        firstBaseline += (rect.size.y - blockHeight) * 0.5f;
    } else if (text.valign == UIText::VAlign::Bottom) {
        firstBaseline += rect.size.y - blockHeight;
    }

    // Each line's baseline and start snap to whole pixels for sharp small labels; glyph
    // advances stay fractional.
    const uint32_t first = static_cast<uint32_t>(out.quads.size());
    uint32_t emitted = 0;
    glm::vec2 drawnTo = nothing;
    float baseline = firstBaseline;
    for (size_t index = 0; index < m_lines.size(); ++index) {
        const std::string_view line = m_lines[index];
        baseline = std::round(firstBaseline + static_cast<float>(index) * lineStep);

        // Walked once: the line's width, then its shadow and its face from the same pens.
        m_placed.clear();
        const auto place = [&](size_t, const FontGlyph& glyph, float pen) {
            if (glyph.size.x > 0.0f && glyph.size.y > 0.0f) m_placed.push_back({&glyph, pen});
            return true;
        };
        const float lineWidth = walkGlyphs(font, line, renderScale, place);
        float lineX = rect.pos.x;
        if (text.align == UIText::Align::Center)     lineX += (rect.size.x - lineWidth) * 0.5f;
        else if (text.align == UIText::Align::Right)  lineX += rect.size.x - lineWidth;
        lineX = std::round(lineX);

        // The line's shadow first, all of it, so no letter's shadow falls on its neighbor.
        const auto emitLine = [&](const glm::vec2& offset, const glm::vec4& color) {
            for (const auto& [glyphOf, pen] : m_placed) {
                const FontGlyph& glyph = *glyphOf;
                const glm::vec2 p0 = offset + glm::vec2{
                    lineX + pen + glyph.offset.x * renderScale,
                    baseline + glyph.offset.y * renderScale
                };
                const glm::vec2 p1 = p0 + glyph.size * renderScale;
                appendQuad(
                    out,
                    p0,
                    p1,
                    glyph.uvMin,
                    glyph.uvMax,
                    faded(color, opacity),
                    faded(color, opacity),
                    glm::vec2(UI_TEXT_MARK, 0.0f),
                    glm::vec4(0.0f),
                    false
                );
                drawnTo = glm::max(drawnTo, p1);
                ++emitted;
            }
        };
        const float drop = text.pixelSize * canvasScale * UIText::SHADOW_DROP;
        if (text.shadow.a > 0.0f) emitLine(glm::vec2(drop), text.shadow);
        emitLine(glm::vec2(0.0f), text.color);
    }

    if (emitted == 0) return nothing;

    appendCommand(out, first, emitted, fontHandle, {}, clip);
    // Measured to the last line's descent, not the lowest ink.
    drawnTo.y = glm::max(drawnTo.y, baseline - font.descent * renderScale);
    return drawnTo;
}

void UISystem::appendShaped(
    UIDrawData& out,
    const UIRect& rect,
    const glm::vec4& color,
    const UIShape& shape,
    float scale,
    bool image
) {
    // Floored at zero: a negative radius is what marks a glyph.
    appendQuad(
        out,
        rect.pos,
        rect.max(),
        glm::vec2(0.0f),
        glm::vec2(1.0f),
        color,
        fadeEnd(color, shape),
        glm::vec2(glm::max(shape.cornerRadius, 0.0f), shape.borderWidth) * scale,
        shape.borderColor,
        image
    );
}

void UISystem::appendQuad(
    UIDrawData& out,
    const glm::vec2& p0,
    const glm::vec2& p1,
    const glm::vec2& uv0,
    const glm::vec2& uv1,
    const glm::vec4& color,
    const glm::vec4& bottom,
    const glm::vec2& shape,
    const glm::vec4& border,
    bool image
) {
    out.quads.push_back(UIQuad{
        glm::vec4(p0, p1),
        glm::vec4(uv0, uv1),
        color,
        bottom,
        border,
        glm::vec3(shape, image ? 1.0f : 0.0f)
    });
}

} // namespace Vkm::Engine
