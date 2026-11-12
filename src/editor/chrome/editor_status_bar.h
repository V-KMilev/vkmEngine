#pragma once

namespace Vkm::Engine {

class ConsolePanel;
struct EditorContext;

/**
 * @brief The editor's bottom status bar, which remembers nothing.
 *
 * The selection's breadcrumb on the left; on the right the errors and the warnings, each a click
 * from its Console view, and the frame rate.
 */
namespace EditorStatusBar {

/**
 * @brief How tall the bar draws.
 *
 * Only inside an ImGui frame, as it reads a font metric.
 *
 * @return Height in screen pixels.
 */
float height();

/**
 * @brief Draw the bar into the current window.
 *
 * @param ec      Editor context whose scene, selection, errors and clock it reads.
 * @param console Counts the warnings, and is shown the view a click asks for.
 */
void draw(EditorContext& ec, ConsolePanel& console);

} // namespace EditorStatusBar

} // namespace Vkm::Engine
