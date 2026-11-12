#pragma once

#include <string>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief What a physics layer is called: the project's name for its bit, else its number.
 *
 * @param names Project::physicsLayers.
 * @param bit   0 for the layer of value 1.
 * @return The name, or "Layer <bit + 1>".
 */
std::string physicsLayerName(const std::vector<std::string>& names, int bit);

/**
 * @brief A combo picking the one physics layer a body is on, None included, filling the control column.
 *
 * @param layer The layer's bit, written on a pick.
 * @param names The project's layer names (Project Settings > Physics Layers).
 * @return Whether a pick changed it.
 */
bool layerCombo(int* layer, const std::vector<std::string>& names);

/**
 * @brief A combo of a checkbox per physics layer, for the layers a body collides with.
 *
 * @param mask  The layers' bits, written as they are ticked.
 * @param names The project's layer names.
 * @return Whether it changed.
 */
bool layerMaskCombo(int* mask, const std::vector<std::string>& names);

} // namespace Vkm::Engine
