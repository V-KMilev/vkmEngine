#pragma once

namespace Vkm::Engine {

class BuildController;
class ConsolePanel;
struct EditorContext;

/**
 * @brief The editor's bottom status bar.
 *
 * A readout of the unsaved-changes dot and the selection (parent breadcrumb +
 * position) on the left; on the right what wants a look - the errors, the
 * warnings, a build running or failed - each a click from its Console view.
 * It remembers nothing.
 */
namespace EditorStatusBar {

/**
 * @brief How tall the bar draws.
 *
 * Only meaningful inside an ImGui frame; it is built from a font metric.
 *
 * @return Height in screen pixels.
 */
float height();

/**
 * @brief Draw the bar into the current window.
 *
 * @param ec      Editor context whose scene, selection, dirty flag and errors it reads.
 * @param console Counts the warnings, and is shown the view a click asks for.
 * @param build   Whether a build is running, or failed.
 */
void draw(EditorContext& ec, ConsolePanel& console, const BuildController& build);

} // namespace EditorStatusBar

} // namespace Vkm::Engine
