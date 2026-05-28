#pragma once

namespace Engine {

/**
 * @brief Tag component marking an entity as "currently selected."
 *
 * Empty by design: presence in the SparseSet is the entire signal.
 * RenderView::build copies this flag onto DrawableData::selected so the
 * outline pass can highlight selected meshes without taking an editor
 * dependency.
 */
struct Selected {};

} // namespace Engine
