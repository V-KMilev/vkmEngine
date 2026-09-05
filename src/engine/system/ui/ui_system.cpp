#include "system/ui/ui_system.h"

#include <algorithm>

#include "ecs/scene.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "core/host_chrome.h"
#include "core/event/event_bus.h"
#include "system/ui/ui_events.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "platform/window/window_manager.h"
#include "platform/input/default_bindings.h"
#include "platform/input/input_map.h"
#include "platform/window/input_handle.h"
#include "debug/profiler.h"

namespace Vkm::Engine {

void UISystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("UISystem");

    m_drawData.clear();
    m_buttonHits.clear();
    m_pointerBlockers.clear();
    ctx.ui = &m_drawData;

    // Element rects resolve in viewport-local framebuffer pixels; GLFW hands
    // back the cursor in window screen coords, which is the same thing only on
    // an unscaled display - hence the scale.
    const MouseInputHandle& mouse = ctx.window.getInputHandle().getMouse();
    const float pointerScale = ctx.window.framebufferScale();
    const HostChrome::ViewportRect vp = ctx.chrome.viewport(ctx.window);
    m_pointer = {
        static_cast<float>(mouse.getX()) * pointerScale - static_cast<float>(vp.x),
        static_cast<float>(mouse.getY()) * pointerScale - static_cast<float>(vp.y)
    };

    // Through the action map, so these edges are the ones every other reader of
    // that button sees. The button reads as up while the host's chrome owns the
    // pointer, so a drag begun over a panel cannot end as a game click.
    const bool pointerIsOurs = !ctx.chrome.capturesPointer();
    m_mouseDown     = pointerIsOurs && ctx.input.held(InputActions::UI_CLICK);
    m_mouseDownEdge = pointerIsOurs && ctx.input.pressed(InputActions::UI_CLICK);
    m_mouseUpEdge   = pointerIsOurs && ctx.input.released(InputActions::UI_CLICK);

    const float vpW = static_cast<float>(vp.width);
    const float vpH = static_cast<float>(vp.height);
    if (vpW <= 0.0f || vpH <= 0.0f) return;

    // Canvases draw - and therefore hit-test - in ascending sortOrder. SparseSet
    // iteration order is arbitrary (and changes on removal), so collect and sort
    // each frame; the entity index breaks ties to keep equal orders stable.
    m_canvases.clear();
    ctx.scene.forEach<UICanvas>([&](EntityId entity, const UICanvas& canvas) {
        if (canvas.visible) m_canvases.push_back(CanvasRef{canvas.sortOrder, entity});
    });
    std::sort(m_canvases.begin(), m_canvases.end(),
        [](const CanvasRef& a, const CanvasRef& b) {
            if (a.sortOrder != b.sortOrder) return a.sortOrder < b.sortOrder;
            return a.entity.slot() < b.entity.slot();
        });

    // Each canvas spans the whole viewport.
    const UIRect viewport{glm::vec2(0.0f), glm::vec2(vpW, vpH)};
    for (const CanvasRef& ref : m_canvases) {
        const UICanvas& canvas = ctx.scene.get<UICanvas>(ref.entity);

        const bool scaleWithHeight = canvas.scaleMode == UICanvas::ScaleMode::ScaleWithHeight;
        const float scale = (scaleWithHeight && canvas.referenceHeight > 0.0f)
            ? vpH / canvas.referenceHeight
            : 1.0f;

        HierarchyOperations::forEachChild(ctx.scene, ref.entity, [&](EntityId child) {
            resolveElement(ctx, child, viewport, scale, 0);
        });
    }

    resolveInteraction(ctx);
}

void UISystem::resolveElement(
    FrameContext& ctx,
    EntityId entity,
    const UIRect& parentRect,
    float scale,
    uint32_t depth
) {
    if (depth >= HierarchyOperations::MAX_DEPTH) {
        HierarchyOperations::detail::warnHierarchyCycle("UI layout");
        return;
    }
    if (!ctx.scene.has<UIElement>(entity)) return;

    UIElement& element = ctx.scene.get<UIElement>(entity);
    if (!element.visible) return;

    const glm::vec2 sizePx   = element.size * scale;
    const glm::vec2 anchorPx = parentRect.pos + element.anchor * parentRect.size;
    element.screenRect = UIRect{anchorPx + element.position * scale - element.pivot * sizePx, sizePx};

    emitImage(ctx, entity);
    emitButton(ctx, entity);
    emitText(ctx, entity, scale);

    // Only what draws can block: a bare UIElement is a layout box with nothing
    // in it, and stopping a click on empty space would surprise everyone. Text
    // does not block either - a label over a button is a caption on it.
    const bool draws = ctx.scene.has<UIImage>(entity) || ctx.scene.has<UIButton>(entity);
    if (draws && element.blocksPointer && !ctx.chrome.capturesPointer()
        && element.screenRect.contains(m_pointer)) {
        m_pointerBlockers.push_back(entity);
    }

    HierarchyOperations::forEachChild(ctx.scene, entity, [&](EntityId child) {
        resolveElement(ctx, child, element.screenRect, scale, depth + 1);
    });
}

void UISystem::appendCommand(uint32_t first, uint32_t count, FontHandle font, UIDrawKind kind) {
    if (!m_drawData.commands.empty()) {
        UIDrawCmd& last = m_drawData.commands.back();
        if (last.kind == kind && last.font == font
                && last.firstVertex + last.vertexCount == first) {
            last.vertexCount += count;
            return;
        }
    }
    m_drawData.commands.push_back(UIDrawCmd{first, count, font, kind});
}

void UISystem::emitImage(FrameContext& ctx, EntityId entity) {
    if (!ctx.scene.has<UIImage>(entity)) return;

    const UIElement& element = ctx.scene.get<UIElement>(entity);
    const UIImage&   image   = ctx.scene.get<UIImage>(entity);

    const uint32_t first = static_cast<uint32_t>(m_drawData.vertices.size());
    appendQuad(element.screenRect.pos, element.screenRect.max(),
               glm::vec2(0.0f), glm::vec2(1.0f), image.color);

    appendCommand(first, 6, {}, UIDrawKind::Solid);
}

void UISystem::emitButton(FrameContext& ctx, EntityId entity) {
    if (!ctx.scene.has<UIButton>(entity)) return;

    const UIButton&  button  = ctx.scene.get<UIButton>(entity);
    const UIElement& element = ctx.scene.get<UIElement>(entity);

    // Emit now to keep painter order; resolveInteraction() recolours the quad
    // once every candidate of the frame is known.
    const uint32_t first = static_cast<uint32_t>(m_drawData.vertices.size());
    appendQuad(element.screenRect.pos, element.screenRect.max(),
               glm::vec2(0.0f), glm::vec2(1.0f), button.colorForState());
    appendCommand(first, 6, {}, UIDrawKind::Solid);

    m_buttonHits.push_back(ButtonHit{
        entity, first,
        button.interactable && !ctx.chrome.capturesPointer()
                            && element.screenRect.contains(m_pointer)});
}

void UISystem::resolveInteraction(FrameContext& ctx) {
    // The draw list is painter-ordered, so the last blocker under the pointer is
    // the one on top, and only it hovers, presses and clicks. Blockers rather than
    // buttons, so an opaque panel over a button wins - a pause menu.
    const EntityId topmost =
        m_pointerBlockers.empty() ? EntityId{} : m_pointerBlockers.back();

    // Published for gameplay, which has to be able to ask before acting on a
    // click of its own.
    m_drawData.pointerOverUI = !m_pointerBlockers.empty();

    // A press starts a click candidate, cleared when the press missed, and a
    // release over that same button fires the click. When the topmost blocker is
    // not a button, nothing is armed and the release matches no hit.
    if (m_mouseDownEdge) {
        // Guarded rather than relying on has(): Scene::has asserts the entity is
        // alive, and with nothing under the pointer `topmost` is a default id.
        const bool onAButton =
            !m_pointerBlockers.empty() && ctx.scene.has<UIButton>(topmost);
        m_pressedButton = onAButton ? topmost : EntityId{};
    }

    for (const ButtonHit& hit : m_buttonHits) {
        UIButton& button = ctx.scene.get<UIButton>(hit.entity);

        const bool isTopmost = hit.inside && hit.entity == topmost;
        const bool held      = isTopmost && m_mouseDown && m_pressedButton == hit.entity;

        if (m_mouseUpEdge && isTopmost && m_pressedButton == hit.entity) {
            ctx.events.enqueue(UIClickEvent{hit.entity, button.eventId});
        }

        if (!button.interactable) button.state = UIButton::State::Disabled;
        else if (held)            button.state = UIButton::State::Pressed;
        else if (isTopmost)       button.state = UIButton::State::Hover;
        else                      button.state = UIButton::State::Normal;

        const glm::vec4& color = button.colorForState();
        for (uint32_t i = 0; i < 6; ++i) {
            m_drawData.vertices[hit.firstVertex + i].color = color;
        }
    }

    // A release ends any press, whether or not it landed on the pressed button.
    if (m_mouseUpEdge) m_pressedButton = {};
}

void UISystem::emitText(FrameContext& ctx, EntityId entity, float canvasScale) {
    if (!ctx.scene.has<UIText>(entity)) return;

    const UIText& text = ctx.scene.get<UIText>(entity);
    if (text.text.empty() || text.font.empty()) return;

    // Resolve the font by name: names are the serializable asset identity, so
    // the component stays plain data (and the lookup is O(1)).
    const FontHandle fontHandle = ctx.resources.findByName<FontAsset>(text.font);
    if (!fontHandle) return;
    const FontAsset& font = ctx.resources.get(fontHandle);
    if (font.pixelHeight <= 0.0f) return;

    const UIElement& element = ctx.scene.get<UIElement>(entity);

    // Map baked-pixel metrics to screen pixels: author size (reference px) times
    // the canvas scale, relative to the height the atlas was baked at.
    const float renderScale = text.pixelSize * canvasScale / font.pixelHeight;

    // Pre-measure the line so Center / Right alignment can offset the pen.
    float lineWidth = 0.0f;
    for (unsigned char c : text.text) {
        if (const FontGlyph* g = font.glyph(c)) lineWidth += g->advance * renderScale;
    }
    float penX = element.screenRect.pos.x;
    if (text.align == UIText::Align::Center)     penX += (element.screenRect.size.x - lineWidth) * 0.5f;
    else if (text.align == UIText::Align::Right)  penX += element.screenRect.size.x - lineWidth;

    // Vertical alignment offsets the baseline by the unused rect height; the
    // text block spans ascent above the baseline to descent (negative) below.
    float baselineY = element.screenRect.pos.y + font.ascent * renderScale;
    const float blockHeight = (font.ascent - font.descent) * renderScale;
    if (text.valign == UIText::VAlign::Middle) {
        baselineY += (element.screenRect.size.y - blockHeight) * 0.5f;
    } else if (text.valign == UIText::VAlign::Bottom) {
        baselineY += element.screenRect.size.y - blockHeight;
    }

    const uint32_t first   = static_cast<uint32_t>(m_drawData.vertices.size());
    uint32_t       emitted = 0;
    for (unsigned char c : text.text) {
        const FontGlyph* g = font.glyph(c);
        if (!g) continue;
        if (g->size.x > 0.0f && g->size.y > 0.0f) {
            const glm::vec2 p0{penX + g->offset.x * renderScale, baselineY + g->offset.y * renderScale};
            appendQuad(p0, p0 + g->size * renderScale, g->uvMin, g->uvMax, text.color);
            emitted += 6;
        }
        penX += g->advance * renderScale;
    }

    if (emitted > 0) {
        appendCommand(first, emitted, fontHandle, UIDrawKind::Text);
    }
}

void UISystem::appendQuad(
    const glm::vec2& p0,
    const glm::vec2& p1,
    const glm::vec2& uv0,
    const glm::vec2& uv1,
    const glm::vec4& color
) {
    const auto push = [&](float x, float y, float u, float v) {
        m_drawData.vertices.push_back(UIVertex{{x, y}, {u, v}, color});
    };

    // Two triangles covering the rect, top-left origin.
    push(p0.x, p0.y, uv0.x, uv0.y);
    push(p1.x, p0.y, uv1.x, uv0.y);
    push(p1.x, p1.y, uv1.x, uv1.y);
    push(p0.x, p0.y, uv0.x, uv0.y);
    push(p1.x, p1.y, uv1.x, uv1.y);
    push(p0.x, p1.y, uv0.x, uv1.y);
}

} // namespace Vkm::Engine
