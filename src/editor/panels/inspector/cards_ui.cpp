#include "panels/inspector/cards.h"

#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_choice.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_scroll.h"
#include "ecs/component/ui/ui_shape.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/component/ui/ui_stack.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/component/ui/ui_toggle.h"
#include "ecs/scene.h"
#include "resource/asset/font_asset.h"
#include "resource/resource_manager.h"
#include "ui/style/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief A combo of the loaded fonts, by the name UIText keeps: engine fonts have no AssetType.
 *
 * An unloaded name is still offered, marked, so opening the card does not reset it.
 *
 * @param resources Lists the fonts.
 * @param name      The font's name, written on a pick.
 * @return Whether a pick changed it.
 */
bool pickFont(const ResourceManager& resources, std::string& name) {
    std::vector<std::string> fonts;
    const int picked = searchCombo("##v", name.empty() ? "(none)" : name.c_str(), [&] {
        resources.forEachOfType<FontAsset>([&](Handle<FontAsset>, const FontAsset& font) {
            if (!font.isHidden()) fonts.push_back(font.name());
        });
        std::vector<ComboRow> rows;
        for (const std::string& font : fonts) rows.push_back({font, font == name});
        // An unloaded name, last and marked, so it does not read as a choice.
        if (!name.empty() && std::find(fonts.begin(), fonts.end(), name) == fonts.end()) {
            rows.push_back({name + "  (not loaded)", true, true});
        }
        return rows;
    });
    if (picked < 0 || static_cast<size_t>(picked) >= fonts.size() || fonts[picked] == name) return false;
    name = fonts[static_cast<size_t>(picked)];
    return true;
}

/**
 * @brief The line every UI content card owes when its entity carries no UIElement.
 *
 * Their rect is the UIElement's; without one UISystem returns from resolveElement first.
 *
 * @param scene Scene holding the entity.
 * @param id    Entity whose card is being drawn.
 */
void warnNoUIElement(const Scene& scene, EntityId id) {
    if (!scene.has<UIElement>(id)) cardNote(Note::Danger, "No UI Element to give it a rect: nothing draws.");
}

// Corners, edge and fade, shared by the UI Image and UI Button cards.
void drawShapeRows(Rows<UIShape>& shape) {
    shape.row<&UIShape::cornerRadius>("%.1f");
    shape.row<&UIShape::borderWidth>("%.1f");
    if (shape.value().borderWidth > 0.0f) shape.row<&UIShape::borderColor>();
    shape.row<&UIShape::gradient>();
    if (shape.value().gradient) shape.row<&UIShape::bottomColor>();
}

} // namespace

CardInfo cardInfo(Tag<UICanvas>) {
    return {"UI Canvas", AddSection::UI, "A layer of UI drawn over the scene", EditorIcon::UICanvas};
}

void drawCard(Rows<UICanvas>& c, CardContext&) {
    c.row<&UICanvas::scaleMode>();
    c.row<&UICanvas::referenceHeight>("%.0f");
    c.row<&UICanvas::sortOrder>();
    c.row<&UICanvas::visible>();
    c.row<&UICanvas::pausesGame>();
    c.row<&UICanvas::toggleAction>();
}

CardInfo cardInfo(Tag<UIElement>) {
    return {"UI Element", AddSection::UI, "A rectangle placed on a canvas", EditorIcon::UIWidget};
}

void drawCard(Rows<UIElement>& c, CardContext& card) {
    c.row<&UIElement::anchor>("%.3f");
    c.row<&UIElement::pivot>("%.3f");
    c.row<&UIElement::position>("%.1f");
    c.row<&UIElement::size>("%.1f");
    c.row<&UIElement::relativeSize>("%.3f");
    c.row<&UIElement::visible>();
    c.slider<&UIElement::opacity>();
    c.row<&UIElement::blocksPointer>();
    c.row<&UIElement::clipChildren>();
    if (!hasCanvasAncestor(card.scene, card.id)) {
        cardNote(Note::Danger, "No UI Canvas above it: nothing draws. Drag it onto a canvas.");
    }
}

CardInfo cardInfo(Tag<UIImage>) {
    return {"UI Image", AddSection::UI, "Fills its rectangle with a color or a picture", EditorIcon::UIImage};
}

void drawCard(Rows<UIImage>& c, CardContext& card) {
    c.row<&UIImage::color>();
    c.row<&UIImage::texture>();
    c.nested<&UIImage::shape>(drawShapeRows);
    warnNoUIElement(card.scene, card.id);
}

CardInfo cardInfo(Tag<UIText>) {
    return {"UI Text", AddSection::UI, "Text in its rectangle", EditorIcon::UIText};
}

void drawCard(Rows<UIText>& c, CardContext& card) {
    const UIText& text = c.value();
    c.row<&UIText::text>();
    const auto fonts = [&](std::string& name, const FieldInfo&) { return pickFont(card.resources, name); };
    c.rowWith<&UIText::font>(fonts);
    c.row<&UIText::pixelSize>("%.0f");
    c.row<&UIText::align>();
    c.row<&UIText::valign>();
    c.row<&UIText::wrap>();
    c.row<&UIText::color>();
    c.row<&UIText::shadow>(nullptr, Shown::Color);
    warnNoUIElement(card.scene, card.id);
    // An unresolved font draws nothing, like a hidden element; the combo marks it only when open.
    if (!card.resources.findByName<FontAsset>(text.font)) {
        cardNote(Note::Danger, "No font named '%s' is loaded: nothing draws.", text.font.c_str());
    }
}

CardInfo cardInfo(Tag<UIButton>) {
    return {"UI Button", AddSection::UI, "Clicked, it sends an event", EditorIcon::UIButton};
}

void drawCard(Rows<UIButton>& c, CardContext& card) {
    c.row<&UIButton::interactable>();
    c.row<&UIButton::normalColor>();
    c.row<&UIButton::hoverColor>();
    c.row<&UIButton::pressedColor>();
    c.row<&UIButton::disabledColor>();
    c.nested<&UIButton::shape>(drawShapeRows);
    warnNoUIElement(card.scene, card.id);
}

CardInfo cardInfo(Tag<UIScroll>) {
    return {"UI Scroll", AddSection::UI, "Scrolls what lies under it", EditorIcon::UIScroll};
}

void drawCard(Rows<UIScroll>& c, CardContext& card) {
    const UIScroll& s = c.value();
    c.row<&UIScroll::offset>("%.0f");
    c.row<&UIScroll::wheelStep>("%.0f");
    warnNoUIElement(card.scene, card.id);
    // Measured by the layout walk, not authored: shown, not edited.
    const glm::vec2 room = s.range();
    const glm::vec2 full = s.contentSize;
    const glm::vec2 seen = s.viewSize;
    ImGui::TextDisabled("Content %.0f x %.0f, view %.0f x %.0f", full.x, full.y, seen.x, seen.y);
    if (room.x <= 0.0f && room.y <= 0.0f) cardNote(Note::Info, "The content fits: nothing to scroll.");
}

CardInfo cardInfo(Tag<UIStack>) {
    return {"UI Stack", AddSection::UI, "Lays its children out in a column or a row", EditorIcon::Levels};
}

void drawCard(Rows<UIStack>& c, CardContext& card) {
    c.row<&UIStack::axis>();
    c.row<&UIStack::spacing>("%.0f");
    warnNoUIElement(card.scene, card.id);
}

CardInfo cardInfo(Tag<UISlider>) {
    return {"UI Slider", AddSection::UI, "A value dragged along a track", EditorIcon::UISlider};
}

void drawCard(Rows<UISlider>& c, CardContext& card) {
    const UISlider& s = c.value();
    // The value's range is the slider's own Min and Max.
    c.rowWith<&UISlider::value>([&](float& value, const FieldInfo& info) {
        FieldInfo bounded = info;
        bounded.min = s.minValue;
        bounded.max = s.maxValue;
        return editFloat(bounded, value, "%.3f", Shown::Plain);
    });
    c.row<&UISlider::minValue>("%.3f");
    c.row<&UISlider::maxValue>("%.3f");
    c.row<&UISlider::step>("%.3f");
    const auto entityRow = [&](const char* label, EntityId& target, const char* tip) {
        if (propEntity(card.scene, card.id, label, target, tip)) c.edited();
    };
    entityRow("Fill", c.custom<&UISlider::fill>(), "Grown to the value: best the track's child");
    entityRow("Knob", c.custom<&UISlider::knob>(), "Stood at the value: best the track's child");
    warnNoUIElement(card.scene, card.id);
    if (!card.scene.has<UIButton>(card.id)) cardNote(Note::Danger, "No UI Button: nothing to drag.");
    if (!(s.maxValue > s.minValue)) cardNote(Note::Danger, "Max is not above Min: it cannot move.");
}

CardInfo cardInfo(Tag<UIToggle>) {
    return {"UI Toggle", AddSection::UI, "On or off", EditorIcon::UIToggle};
}

void drawCard(Rows<UIToggle>& c, CardContext& card) {
    c.row<&UIToggle::on>();
    c.row<&UIToggle::onColor>();
    const char* knobTip = "At its parent's left end when off, the right when on: best the button's child";
    if (propEntity(card.scene, card.id, "Knob", c.custom<&UIToggle::knob>(), knobTip)) c.edited();
    warnNoUIElement(card.scene, card.id);
    if (!card.scene.has<UIButton>(card.id)) cardNote(Note::Danger, "No UI Button: nothing to click.");
}

CardInfo cardInfo(Tag<UIChoice>) {
    return {"UI Choice", AddSection::UI, "One of a list, stepped with arrows", EditorIcon::UIChoice};
}

void drawCard(Rows<UIChoice>& c, CardContext& card) {
    UIChoice& choice = c.value();
    // One a line: the list reads as one and edits as text. A list is the active entity's alone.
    c.rowWith<&UIChoice::options>([&](std::vector<std::string>& options, const FieldInfo&) {
        std::string lines;
        for (size_t i = 0; i < options.size(); ++i) lines += (i > 0 ? "\n" : "") + options[i];
        const ImVec2 box(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 5.0f);
        if (!ImGui::InputTextMultiline("##v", &lines, box)) return false;
        lines.erase(std::remove(lines.begin(), lines.end(), '\r'), lines.end());
        options.clear();
        size_t start = 0;
        for (size_t end; (end = lines.find('\n', start)) != std::string::npos; start = end + 1) {
            options.push_back(lines.substr(start, end - start));
        }
        if (!lines.empty()) options.push_back(lines.substr(start));
        return true;
    });
    const int last = std::max(static_cast<int>(choice.options.size()) - 1, 0);
    const auto steps = [&](int& index, const FieldInfo&) { return sliderInt("##v", &index, 0, last); };
    c.rowWith<&UIChoice::index>(steps);
    const auto entityRow = [&](const char* label, EntityId& target, const char* tip) {
        if (propEntity(card.scene, card.id, label, target, tip)) c.edited();
    };
    entityRow("Back", c.custom<&UIChoice::back>(), "The UI Button stepping toward the first option");
    entityRow("Next", c.custom<&UIChoice::next>(), "The UI Button stepping toward the last");
    entityRow("Text", c.custom<&UIChoice::text>(), "Its UI Text shows the option: best a child");

    warnNoUIElement(card.scene, card.id);
    const bool back = card.scene.has<UIButton>(choice.back);
    const bool next = card.scene.has<UIButton>(choice.next);
    if (!back && !next) cardNote(Note::Danger, "Back and Next are no UI Buttons: nothing steps it.");
    else if (!back)     cardNote(Note::Danger, "Back is no UI Button: that arrow does nothing.");
    else if (!next)     cardNote(Note::Danger, "Next is no UI Button: that arrow does nothing.");
    if (choice.index < 0 || choice.index >= static_cast<int>(choice.options.size())) {
        cardNote(Note::Warning, "Index names no option: it shows nothing.");
    }
}

} // namespace Vkm::Engine
