#pragma once

#include <glm/glm.hpp>

#include "resource/asset/texture_asset.h"
#include "resource/resource_handle.h"

namespace Engine {

class ResourceManager;

/**
 * @brief Create a persistent solid-color texture (serializes into the scene).
 *
 * Stamps a `{"kind":"solid","color":[r,g,b,a],"srgb":bool}` source descriptor -
 * the recipe the cooker records in the asset library - and a deterministic name
 * keyed on the same color, which is the identity a scene reference resolves by.
 * Use for user-authored procedural textures (the Material Editor's "Generate
 * texture" action).
 *
 * @param color RGBA color (0-1 range).
 * @param resourceManager Resource manager to add the texture to.
 * @param srgb Whether to treat the color as sRGB.
 * @return Handle to the generated texture.
 */
TextureHandle createSolidColorTexture(
    glm::vec4 color,
    ResourceManager& resourceManager,
    bool srgb = false
);

/**
 * @brief Generate a white texture (1,1,1,1).
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the white texture.
 */
TextureHandle generateWhiteTexture(ResourceManager& resourceManager);

/**
 * @brief Generate a black texture (0,0,0,1).
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the black texture.
 */
TextureHandle generateBlackTexture(ResourceManager& resourceManager);

/**
 * @brief Generate a default normal map texture.
 *
 * 1x1, (0.5, 0.5, 1.0) in texture space: straight up, so a surface sampling it
 * gets no normal perturbation.
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the default normal map.
 */
TextureHandle generateNormalTexture(ResourceManager& resourceManager);

/**
 * @brief Generate a gray texture (0.5, 0.5, 0.5, 1).
 *
 * Useful for roughness/metallic maps that default to middle values.
 *
 * @param resourceManager Resource manager to add the texture to.
 * @return Handle to the gray texture.
 */
TextureHandle generateGrayTexture(ResourceManager& resourceManager);

} // namespace Engine
