#include "gl_image_upload.h"

#include <GL/glew.h>

#include "decoded_image.h"

namespace Vkm::Engine {

std::unique_ptr<Vkm::OpenGL::Texture2D> uploadImageFile(const std::string& path) {
    const DecodedImage image = decodeImageRGBA(path);
    if (!image.isValid()) return nullptr;
    return uploadImage(image, path);
}

std::unique_ptr<Vkm::OpenGL::Texture2D> uploadImage(const DecodedImage& image, const std::string& name) {
    Vkm::OpenGL::Texture2DParams params;
    params.width           = image.width;
    params.height          = image.height;
    params.internalFormat  = GL_SRGB8_ALPHA8;
    params.generateMipmaps = true;
    params.minFilter       = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
    params.data            = image.pixels.data();
    return std::make_unique<Vkm::OpenGL::Texture2D>(name, params);
}

} // namespace Vkm::Engine
