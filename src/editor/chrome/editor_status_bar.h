#pragma once

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief The editor's bottom status bar.
 *
 * A readout of the unsaved-changes dot, the selection (parent breadcrumb +
 * position) and the build banner; it remembers nothing.
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
 * @param ec Editor context whose scene, selection and dirty flag it reads.
 */
void draw(EditorContext& ec);

} // namespace EditorStatusBar

} // namespace Vkm::Engine
