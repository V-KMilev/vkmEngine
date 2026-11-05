#pragma once

#include <memory>
#include <string>

#include "vkm_opengl/texture/texture.h"

namespace Vkm::Engine {

struct DecodedImage;

/**
 * @brief Decode the image at @p path and upload it as sRGB color, with a mip chain.
 *
 * For a picture the host shows and no asset describes, so it is read straight off disk. The
 * chain keeps an image drawn several times smaller than its pixels from shedding its fine
 * strokes. Rows arrive bottom-up (see decodeImageRGBA); a drawer whose UVs run the other way
 * flips them at the draw.
 *
 * @param path Image file to read.
 * @return The texture, or null when the file does not decode.
 */
std::unique_ptr<Vkm::OpenGL::Texture2D> uploadImageFile(const std::string& path);

/**
 * @brief uploadImageFile's upload, for pixels already decoded.
 *
 * @param image The decoded pixels; isValid().
 * @param name  What the texture is called, for the debugger.
 * @return The texture.
 */
std::unique_ptr<Vkm::OpenGL::Texture2D> uploadImage(const DecodedImage& image, const std::string& name);

} // namespace Vkm::Engine
