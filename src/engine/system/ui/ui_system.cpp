#include "system/ui/ui_system.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ecs/scene.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "core/host_chrome.h"
#include "core/event/event_bus.h"
#include "system/ui/text_layout.h"
#include "system/ui/ui_events.h"
#include "platform/window/window_manager.h"
#include "platform/input/input_map.h"
#include "debug/profiler.h"

namespace Vkm::Engine {

namespace {

// Float equality is right: a clip is copied down the walk, not recomputed, so runs
// under one panel hold bit-identical rects.
bool sameClip(const UIRect& a, const UIRect& b) {
    return a.pos == b.pos && a.size == b.size;
}

// What a subtree that drew nothing reports: below every corner, so it moves no max.
glm::vec2 nothingDrawn() {
    return glm::vec2(std::numeric_limits<float>::lowest());
}

const glm::vec4& fadeEnd(const glm::vec4& color, const UIShape& shape) {
    return shape.gradient ? shape.bottomColor : color;
}

} // namespace

void UISystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("UISystem");

    m_drawData.clear();
    m_buttonHits.clear();
    m_pointerBlockers.clear();
    m_scrollTargets.clear();
    ctx.ui = &m_drawData;
    ctx.input.setPointerOverUI(false);

    // Rects are in framebuffer pixels, the cursor in window pixels; they differ on a
    // scaled display.
    const float pointerScale = ctx.window.framebufferScale();
    const HostChrome::ViewportRect vp = ctx.chrome.viewport(ctx.window);
    m_pointer = ctx.input.pointer() * pointerScale
        - glm::vec2(static_cast<float>(vp.x), static_cast<float>(vp.y));

    // A disabled cursor is a look control whose virtual position points at nothing,
    // so a HUD it crosses must not take the click.
    m_cursorFree = ctx.window.cursorMode() != CursorMode::Disabled;

    // Resolved whoever owns the pointer, so a host can still ask pointerTarget. The
    // map already hides buttons and wheel under the chrome, so only hover asks it.
    m_pointerIsOurs = m_cursorFree && !ctx.chrome.capturesPointer();
    m_mouseDown     = ctx.input.held(InputActions::UI_CLICK);
    m_mouseDownEdge = ctx.input.pressed(InputActions::UI_CLICK);
    m_mouseUpEdge   = ctx.input.released(InputActions::UI_CLICK);

    // The chrome taking the pointer disarms the press: kept armed, a press let go over
    // a panel could become a click on a later release.
    if (!m_pointerIsOurs) m_pressedButton = {};

    // Every notch: gameplay's wheel() reads zero over a blocker, so they come here.
    m_wheel = ctx.input.uiWheel();

    const float vpW = static_cast<float>(vp.width);
    const float vpH = static_cast<float>(vp.height);
    if (vpW <= 0.0f || vpH <= 0.0f) return;

    // SparseSet order is arbitrary; the entity index breaks ties for stability.
    m_canvases.clear();
    ctx.scene.forEach<UICanvas>([&](EntityId entity, const UICanvas& canvas) {
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

    const UIRect viewport{glm::vec2(0.0f), glm::vec2(vpW, vpH)};
    for (const CanvasRef& ref : m_canvases) {
        const UICanvas& canvas = ctx.scene.get<UICanvas>(ref.entity);

        const bool scaleWithHeight = canvas.scaleMode == UICanvas::ScaleMode::ScaleWithHeight;
        const float scale = (scaleWithHeight && canvas.referenceHeight > 0.0f)
            ? vpH / canvas.referenceHeight
            : 1.0f;

        // The viewport is both the top-level parent rect and the starting clip.
        HierarchyOperations::forEachChild(ctx.scene, ref.entity, [&](EntityId child) {
            resolveElement(ctx, child, viewport, viewport, scale, 0);
        });
    }

    resolveInteraction(ctx);
}

glm::vec2 UISystem::resolveElement(
    FrameContext& ctx,
    EntityId entity,
    const UIRect& parentRect,
    const UIRect& clip,
    float scale,
    uint32_t depth
) {
    if (depth >= HierarchyOperations::MAX_DEPTH) {
        HierarchyOperations::warnWalkBound("UI layout", HierarchyOperations::MAX_DEPTH);
        return nothingDrawn();
    }
    UIElement* held = ctx.scene.tryGet<UIElement>(entity);
    if (!held) return nothingDrawn();

    UIElement& element = *held;
    if (!element.visible) return nothingDrawn();

    const glm::vec2 sizePx   = glm::max(
        element.size * scale + element.relativeSize * parentRect.size,
        glm::vec2(0.0f)
    );
    const glm::vec2 anchorPx = parentRect.pos + element.anchor * parentRect.size;
    element.screenRect = UIRect{anchorPx + element.position * scale - element.pivot * sizePx, sizePx};

    // Clipped away is out of reach: a row scrolled out of sight takes no click.
    const bool pointerHere = m_cursorFree && clip.intersected(element.screenRect).contains(m_pointer);

    const bool drewImage    = emitImage(ctx, entity, element, clip, scale);
    const bool drewButton   = emitButton(ctx, entity, element, clip, scale);
    const glm::vec2 textMax = emitText(ctx, entity, element, clip, scale);

    // Only what draws can block; text over a button is a caption on it.
    const bool draws = drewImage || drewButton;
    if (draws && element.blocksPointer && pointerHere) m_pointerBlockers.push_back(entity);

    UIScroll*    scroll    = ctx.scene.tryGet<UIScroll>(entity);
    const bool   clips     = scroll != nullptr || element.clipChildren;
    const UIRect childClip = clips ? clip.intersected(element.screenRect) : clip;
    UIRect       childRect = element.screenRect;

    if (scroll) {
        // Laid out at the offset clamped to last frame's content; the stored offset is
        // clamped below, or one set the frame a row was added would stop short for good.
        scroll->viewSize = element.screenRect.size / scale;
        childRect.pos   -= glm::clamp(scroll->offset, glm::vec2(0.0f), scroll->range()) * scale;

        // A scroll view always clips, so its window is the rect pointerHere tested.
        if (pointerHere) m_scrollTargets.push_back(ScrollTarget{entity, m_pointerBlockers.size()});
    }

    glm::vec2 contentMax = nothingDrawn();
    HierarchyOperations::forEachChild(ctx.scene, entity, [&](EntityId child) {
        contentMax = glm::max(contentMax, resolveElement(ctx, child, childRect, childClip, scale, depth + 1));
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
    uint32_t first,
    uint32_t count,
    FontHandle font,
    TextureHandle image,
    const UIRect& clip
) {
    if (!m_drawData.commands.empty()) {
        UIDrawCmd& last = m_drawData.commands.back();
        const bool sameAtlas = !font || !last.font || last.font == font;
        const bool sameImage = !image || !last.image || last.image == image;
        if (sameAtlas && sameImage && sameClip(last.clip, clip)
            && last.firstVertex + last.vertexCount == first) {
            last.vertexCount += count;
            if (font)  last.font  = font;
            if (image) last.image = image;
            return;
        }
    }
    m_drawData.commands.push_back(UIDrawCmd{first, count, clip, font, image});
}

bool UISystem::emitImage(
    FrameContext& ctx,
    EntityId entity,
    const UIElement& element,
    const UIRect& clip,
    float canvasScale
) {
    const UIImage* image = ctx.scene.tryGet<UIImage>(entity);
    if (!image) return false;

    const uint32_t first    = static_cast<uint32_t>(m_drawData.vertices.size());
    const bool     textured = static_cast<bool>(image->texture);
    appendShaped(element.screenRect, image->color, image->shape, canvasScale, textured);
    appendCommand(first, 6, {}, image->texture, clip);
    return true;
}

bool UISystem::emitButton(
    FrameContext& ctx,
    EntityId entity,
    const UIElement& element,
    const UIRect& clip,
    float canvasScale
) {
    UIButton* button = ctx.scene.tryGet<UIButton>(entity);
    if (!button) return false;

    // In the element's authored space, free of canvas scale and viewport offset.
    button->pointer      = (m_pointer - element.screenRect.pos) / canvasScale;
    button->resolvedSize = element.screenRect.size / canvasScale;

    const uint32_t first = static_cast<uint32_t>(m_drawData.vertices.size());
    appendShaped(element.screenRect, button->colorForState(), button->shape, canvasScale, false);
    appendCommand(first, 6, {}, {}, clip);

    m_buttonHits.push_back(ButtonHit{entity, first});
    return true;
}

void UISystem::resolveInteraction(FrameContext& ctx) {
    const EntityId topmost = m_pointerBlockers.empty() ? EntityId{} : m_pointerBlockers.back();

    m_drawData.pointerTarget = topmost;
    ctx.input.setPointerOverUI(static_cast<bool>(topmost));

    if (m_mouseDownEdge) {
        // has() answers false for the null `topmost`, so no separate check.
        const bool onAButton = ctx.scene.has<UIButton>(topmost);
        m_pressedButton = onAButton ? topmost : EntityId{};
    }

    for (const ButtonHit& hit : m_buttonHits) {
        UIButton& button = ctx.scene.get<UIButton>(hit.entity);

        // The topmost blocker is under the pointer by construction.
        const bool isTopmost = button.interactable && m_pointerIsOurs && hit.entity == topmost;
        button.held          = button.interactable && m_mouseDown && m_pressedButton == hit.entity;
        const bool held      = isTopmost && button.held;

        if (m_mouseUpEdge && isTopmost && m_pressedButton == hit.entity) {
            ctx.events.enqueue(UIClickEvent{hit.entity, button.eventId});
        }

        if (!button.interactable) button.state = UIButton::State::Disabled;
        else if (held)            button.state = UIButton::State::Pressed;
        else if (isTopmost)       button.state = UIButton::State::Hover;
        else                      button.state = UIButton::State::Normal;

        const glm::vec4& color = button.colorForState();
        recolourQuad(hit.firstVertex, color, fadeEnd(color, button.shape));
    }

    if (m_mouseUpEdge) m_pressedButton = {};

    // The innermost window entered after the topmost blocker lies in or over it, so
    // takes the wheel. Failing one, the wheel goes to the nearest scroll view holding
    // the blocker, so a panel over a list takes the wheel as well as the click.
    if (m_wheel != 0.0f) {
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
                scroll->offset[axis] - m_wheel * scroll->wheelStep,
                0.0f,
                room[axis]
            );
        }
    }
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
    EntityId entity,
    const UIElement& element,
    const UIRect& clip,
    float canvasScale
) {
    const glm::vec2 nothing = nothingDrawn();

    const UIText* held = ctx.scene.tryGet<UIText>(entity);
    if (!held) return nothing;

    const UIText& text = *held;
    if (text.text.empty() || text.font.empty()) return nothing;

    // By name, the serializable identity, so the component stays plain data.
    const FontHandle fontHandle = ctx.resources.findByName<FontAsset>(text.font);
    if (!fontHandle) return nothing;
    const FontAsset& font = ctx.resources.get(fontHandle);
    if (font.pixelHeight <= 0.0f) return nothing;

    const float renderScale = text.pixelSize * canvasScale / font.pixelHeight;

    breakLines(text.text, font, renderScale, element.screenRect.size.x, text.wrap);
    if (m_lines.empty()) return nothing;

    // The block spans ascent above the first baseline to descent (negative) below the
    // last; vertical alignment offsets it by the unused height.
    const float lineHeight  = font.lineHeight > 0.0f ? font.lineHeight : font.ascent - font.descent;
    const float lineStep    = lineHeight * renderScale;
    const float blockHeight = (font.ascent - font.descent) * renderScale
        + static_cast<float>(m_lines.size() - 1) * lineStep;

    float firstBaseline = element.screenRect.pos.y + font.ascent * renderScale;
    if (text.valign == UIText::VAlign::Middle) {
        firstBaseline += (element.screenRect.size.y - blockHeight) * 0.5f;
    } else if (text.valign == UIText::VAlign::Bottom) {
        firstBaseline += element.screenRect.size.y - blockHeight;
    }

    // Each line's baseline and start snap to whole pixels for sharp small labels; glyph
    // advances stay fractional.
    const uint32_t first    = static_cast<uint32_t>(m_drawData.vertices.size());
    uint32_t       emitted  = 0;
    glm::vec2      drawnTo  = nothing;
    float          baseline = firstBaseline;
    for (size_t index = 0; index < m_lines.size(); ++index) {
        const std::string_view line = m_lines[index];
        baseline = std::round(firstBaseline + static_cast<float>(index) * lineStep);

        const float lineWidth = walkGlyphs(
            font,
            line,
            renderScale,
            [](size_t, const FontGlyph&, float) { return true; }
        );
        float lineX = element.screenRect.pos.x;
        if (text.align == UIText::Align::Center)     lineX += (element.screenRect.size.x - lineWidth) * 0.5f;
        else if (text.align == UIText::Align::Right)  lineX += element.screenRect.size.x - lineWidth;
        lineX = std::round(lineX);

        walkGlyphs(font, line, renderScale, [&](size_t, const FontGlyph& glyph, float pen) {
            if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f) return true;
            const glm::vec2 p0{
                lineX + pen + glyph.offset.x * renderScale,
                baseline + glyph.offset.y * renderScale
            };
            const glm::vec2 p1 = p0 + glyph.size * renderScale;
            appendQuad(
                p0,
                p1,
                glyph.uvMin,
                glyph.uvMax,
                text.color,
                text.color,
                glm::vec2(UI_TEXT_MARK, 0.0f),
                glm::vec4(0.0f),
                false
            );
            drawnTo = glm::max(drawnTo, p1);
            emitted += 6;
            return true;
        });
    }

    if (emitted == 0) return nothing;

    appendCommand(first, emitted, fontHandle, {}, clip);
    // Measured to the last line's descent, not the lowest ink.
    drawnTo.y = glm::max(drawnTo.y, baseline - font.descent * renderScale);
    return drawnTo;
}

void UISystem::appendShaped(
    const UIRect& rect,
    const glm::vec4& color,
    const UIShape& shape,
    float scale,
    bool image
) {
    // Floored at zero: a negative radius is what marks a glyph.
    appendQuad(
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
    const uint32_t  first   = static_cast<uint32_t>(m_drawData.vertices.size());
    const glm::vec4 geometry(p1 - p0, shape);
    const float     sampled = image ? 1.0f : 0.0f;
    const auto push = [&](float x, float y, float u, float v) {
        m_drawData.vertices.push_back(UIVertex{{x, y}, {u, v}, glm::vec4(0.0f), geometry, border, sampled});
    };

    // Two triangles covering the rect, top-left origin, in the corner order
    // recolourQuad reads: TL TR BR, TL BR BL.
    push(p0.x, p0.y, uv0.x, uv0.y);
    push(p1.x, p0.y, uv1.x, uv0.y);
    push(p1.x, p1.y, uv1.x, uv1.y);

    push(p0.x, p0.y, uv0.x, uv0.y);
    push(p1.x, p1.y, uv1.x, uv1.y);
    push(p0.x, p1.y, uv0.x, uv1.y);

    recolourQuad(first, color, bottom);
}

void UISystem::recolourQuad(uint32_t first, const glm::vec4& top, const glm::vec4& bottom) {
    UIVertex* quad = &m_drawData.vertices[first];
    quad[0].color = quad[1].color = quad[3].color = top;
    quad[2].color = quad[4].color = quad[5].color = bottom;
}

} // namespace Vkm::Engine
