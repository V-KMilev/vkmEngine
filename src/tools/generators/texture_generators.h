#pragma once

#include <glm/glm.hpp>

#include "texture_asset.h"
#include "resource_handle.h"

namespace Engine {
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Generate a solid color texture.
 * 
 * Creates a 1x1 texture with the specified color.
 * Useful for procedural materials or testing.
 * 
 * @param color RGBA color (0-1 range).
 * @param resourceManager Resource manager to add the texture to.
 * @param srgb Whether to use sRGB color space.
 * @return Handle to the generated texture.
 */
TextureHandle generateSolidColorTexture(
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
 * Creates a 1x1 normal map pointing straight up (0.5, 0.5, 1.0 in texture space).
 * This represents a flat surface with no normal perturbation.
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
