#pragma once

#include <cstdint>

namespace Engine {

enum class TextureInternalFormat : uint8_t {
    R8,
    RG8,
    RGB8,
    RGBA8,
    SRGB8,
    SRGBA8,
    RGBA16F,
    RGBA32F
};

enum class TexturePixelFormat : uint8_t {
    R,
    RG,
    RGB,
    RGBA
};

enum class TexturePixelType : uint8_t {
    UnsignedByte,
    Float,
    HalfFloat
};

enum class TextureWrapMode : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};

enum class TextureMinFilter : uint8_t {
    Nearest,
    Linear,
    NearestMipmapNearest,
    LinearMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapLinear
};

enum class TextureMagFilter : uint8_t {
    Nearest,
    Linear
};

struct TextureParams {
    uint32_t width  = 0;
    uint32_t height = 0;

    TextureInternalFormat internalFormat = TextureInternalFormat::RGBA8;
    TexturePixelFormat    format         = TexturePixelFormat::RGBA;
    TexturePixelType      type           = TexturePixelType::UnsignedByte;

    TextureWrapMode  wrapS     = TextureWrapMode::ClampToEdge;
    TextureWrapMode  wrapT     = TextureWrapMode::ClampToEdge;
    TextureMinFilter minFilter = TextureMinFilter::LinearMipmapLinear;
    TextureMagFilter magFilter = TextureMagFilter::Linear;

    bool generateMipmaps = true;
};

} // namespace Engine
