#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include "resource/asset/texture_asset.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

class ResourceManager;

/**
 * @brief Create a persistent solid-color texture, one a scene references by name.
 *
 * Stamps a `{"kind":"solid","color":[r,g,b,a],"usage":<usage>}` recipe and a
 * deterministic name keyed on the same color and usage.
 *
 * @param color RGBA color (0-1 range).
 * @param resourceManager Resource manager to add the texture to.
 * @param usage sRGB colour, linear data, or a normal's x and y.
 * @return Handle to the generated texture.
 */
TextureHandle createSolidColorTexture(
    glm::vec4 color,
    ResourceManager& resourceManager,
    TextureUsage usage = TextureUsage::Data
);

/**
 * @brief The white solid texture (1,1,1,1), linear data.
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the white texture.
 */
TextureHandle generateWhiteTexture(ResourceManager& resourceManager);

/**
 * @brief The black solid texture (0,0,0,1), linear data.
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the black texture.
 */
TextureHandle generateBlackTexture(ResourceManager& resourceManager);

/**
 * @brief The flat normal map: a solid normal texture whose x and y are 128.
 *
 * The rebuilt z points straight up: no perturbation.
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the default normal map.
 */
TextureHandle generateNormalTexture(ResourceManager& resourceManager);

/**
 * @brief The gray solid texture (0.5, 0.5, 0.5, 1), linear data.
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the gray texture.
 */
TextureHandle generateGrayTexture(ResourceManager& resourceManager);

/**
 * @brief Rebuild a texture from the recipe one of the generators above stamped.
 *
 * @param source A `solid` source descriptor.
 * @param resourceManager Resource manager to add the texture to.
 * @return The texture, or an invalid handle for a recipe no generator here wrote.
 */
TextureHandle createGeneratedTexture(const nlohmann::json& source, ResourceManager& resourceManager);

} // namespace Vkm::Engine
