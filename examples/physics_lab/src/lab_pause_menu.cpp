#include "lab_pause_menu.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "core/clock.h"
#include "core/event/event_bus.h"
#include "ecs/component/ui/ui_button.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/component/ui/ui_image.h"
#include "ecs/component/ui/ui_text.h"
#include "ecs/scene.h"
#include "io/project.h"
#include "io/project_paths.h"
#include "net/net_session.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "system/ui/ui_events.h"

namespace Lab {

namespace {

constexpr const char* ACTION_MENU = "Menu";

const glm::vec4 INK   {0.03f, 0.04f, 0.08f, 0.92f};
const glm::vec4 WHITE {1.00f, 1.00f, 1.00f, 1.00f};
const glm::vec4 GREY  {0.62f, 0.68f, 0.78f, 1.00f};
const glm::vec4 ACCENT{0.14f, 0.62f, 0.86f, 0.96f};
const glm::vec4 HOT   {0.24f, 0.78f, 1.00f, 0.98f};

} // namespace

void LabPauseMenu::onStart() {
    // Offline only: in a session the clock is the session's, client spawns take
    // server-owned slots, and a restart would load a scene under every player.
    if (!net().isOffline()) return;

    input().define(ACTION_MENU, { InputBinding{InputSource::Key, GLFW_KEY_ESCAPE, 1.0f} });

    // Built now, while time runs: a behavior spawned while paused would never start.
    m_canvas = spawn("Pause Menu");
    UICanvas canvas;
    canvas.sortOrder = 20;
    canvas.visible   = false;
    scene().add(m_canvas, std::move(canvas));

    m_panel = spawn("Panel", m_canvas);
    scene().add(m_panel, UIElement::at({0.5f, 0.5f}, {0.0f, 0.0f}, {420.0f, 260.0f}));
    scene().add(m_panel, UIImage{INK, {}, {}});

    const EntityId title = spawn("Title", m_panel);
    scene().add(title, UIElement::at({0.5f, 0.0f}, {0.0f, 28.0f}, {380.0f, 48.0f}));
    UIText titleText;
    titleText.text      = "PAUSED";
    titleText.pixelSize = 40.0f;
    titleText.color     = WHITE;
    titleText.align     = UIText::Align::Center;
    scene().add(title, std::move(titleText));

    const auto button = [&](const char* name, const char* label, const char* event, float y) {
        const EntityId id = spawn(name, m_panel);
        scene().add(id, UIElement::at({0.5f, 0.0f}, {0.0f, y}, {240.0f, 52.0f}));
        UIButton btn;
        btn.eventId     = event;
        btn.normalColor = ACCENT;
        btn.hoverColor  = HOT;
        scene().add(id, std::move(btn));
        UIText caption;
        caption.text      = label;
        caption.pixelSize = 22.0f;
        caption.color     = WHITE;
        caption.align     = UIText::Align::Center;
        caption.valign    = UIText::VAlign::Middle;
        scene().add(id, std::move(caption));
        return id;
    };
    button("Resume",  "RESUME",  "lab:resume",  100.0f);
    button("Restart", "RESTART", "lab:restart", 164.0f);

    const EntityId hint = spawn("Hint", m_panel);
    scene().add(hint, UIElement::at({0.5f, 0.0f}, {0.0f, 224.0f}, {380.0f, 22.0f}));
    UIText hintText;
    hintText.text      = "Esc closes this too";
    hintText.pixelSize = 15.0f;
    hintText.color     = GREY;
    hintText.align     = UIText::Align::Center;
    scene().add(hint, std::move(hintText));

    subscribe([this](const UIClickEvent& e) {
        if (e.eventId == "lab:resume") setOpen(false);
        else if (e.eventId == "lab:restart") {
            // Not window()->requestClose(): under the editor that ends the editor.
            // Unpause first: the clock survives the load and this behavior does not.
            setOpen(false);
            Project project;
            if (loadProject(ProjectPaths::projectRoot(), project) && !project.entryScene.empty()) {
                loadScene(project.entryScene);
            }
        }
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

    if (UIImage* panel = scene().tryGet<UIImage>(m_panel)) {
        panel->color.a = INK.a * m_fade;
    }
}

void LabPauseMenu::setOpen(bool open) {
    m_open = open;
    scene().get<UICanvas>(m_canvas).visible = open;
    clock().setPaused(open);
}

} // namespace Lab
