#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include <glm/glm.hpp>

#include "resource_handle.h"

namespace Engine {

/**
 * @brief Represents a single vertex in a mesh.
 *
 * Contains all necessary per-vertex attributes required for rendering and shading,
 * including position, surface normal, UV coordinates, and a tangent vector for normal mapping.
 */
struct Vertex {
    glm::vec3 position;    ///< 3D position of the vertex in model space (x, y, z).
    glm::vec3 normal;      ///< Surface normal at the vertex (used for lighting).
    glm::vec2 uv;          ///< 2D texture coordinates (u, v) for mapping textures.
    glm::vec4 tangent;     ///< Tangent vector (x, y, z, w), used for normal mapping; w is handedness.
};

/**
 * @brief Enum of supported texture formats.
 */
enum class TextureFormat {
    RGBA8,    ///< 8-bit unsigned per channel, 4 channels (standard color)
};

/**
 * @brief Enum of supported texture wrap modes.
 */
enum class TextureWrap {
    Repeat,            ///< Texture repeats (tiles)
    MirroredRepeat,    ///< Texture mirrors and repeats
    ClampToEdge        ///< Texture clamps to edge pixel color
};

/**
 * @brief Enum of supported texture minification filters.
 */
enum class TextureMinFilter {
    Nearest,                 ///< Nearest neighbor sampling (no interpolation)
    Linear,                  ///< Linear interpolation
    NearestMipmapNearest,    ///< Nearest mipmap, nearest texel
    LinearMipmapNearest,     ///< Nearest mipmap, linear texel
    NearestMipmapLinear,     ///< Linear mipmap, nearest texel
    LinearMipmapLinear       ///< Linear mipmap, linear texel (trilinear filtering)
};

/**
 * @brief Enum of supported texture magnification filters.
 */
enum class TextureMagFilter {
    Nearest,    ///< Nearest neighbor sampling (no interpolation)
    Linear      ///< Linear interpolation
};

/**
 * @brief Describes a mesh asset.
 *
 * Holds geometry vertex data, index data, axis-aligned bounding box, and asset version.
 */
struct MeshAsset {
    std::vector<Vertex> vertices;     ///< Vertex buffer (geometry)
    std::vector<uint32_t> indices;    ///< Index buffer (triangle indices)

    // Optional metadata
    glm::vec3 boundsMin{0};           ///< Minimum AABB point in local space
    glm::vec3 boundsMax{0};           ///< Maximum AABB point in local space

    uint64_t version = 1;             ///< Asset version for change tracking
};

/**
 * @brief Describes a texture asset.
 *
 * Holds texture image data and parameters for texture creation and sampling.
 * Includes dimensions, format, wrapping/filtering modes, mipmap generation, color space, and version.
 */
struct TextureAsset {
    uint32_t width = 0;
    uint32_t height = 0;

    TextureFormat format = TextureFormat::RGBA8;
    TextureWrap wrapS = TextureWrap::ClampToEdge;
    TextureWrap wrapT = TextureWrap::ClampToEdge;
    TextureMinFilter minFilter = TextureMinFilter::Linear;
    TextureMagFilter magFilter = TextureMagFilter::Linear;

    bool generateMipmaps = false;
    bool srgb = false;

    std::vector<uint8_t> data;

    uint64_t version = 1;
};

/**
 * @brief Describes a material asset.
 *
 * Holds PBR material scalar properties and references to associated textures.
 * Supports both rasterization and raytracing rendering pipelines.
 */
struct MaterialAsset {
    // Base PBR properties
    glm::vec4 albedo   = {1,1,1,1};              ///< Base albedo color (RGBA)
    glm::vec3 emission = {0,0,0};                ///< Emissive color (RGB)
    float metallic     = 0.0f;                   ///< Metalness (0: dielectric, 1: metallic)
    float roughness    = 0.5f;                   ///< Surface roughness (0: smooth, 1: rough)

    // Essential for raytracing and advanced PBR
    float ior          = 1.5f;                   ///< Index of refraction (1.0: air, 1.5: glass, 2.4: diamond)
    float transmission = 0.0f;                   ///< Transmission factor (0: opaque, 1: fully transparent)
    float alpha        = 1.0f;                   ///< Alpha/opacity (0: transparent, 1: opaque)
    float ao           = 1.0f;                   ///< Ambient occlusion factor (0: fully occluded, 1: no occlusion)

    // Advanced PBR properties
    float clearcoat               = 0.0f;        ///< Clearcoat layer strength (0: none, 1: full)
    float clearcoatRoughness      = 0.0f;        ///< Clearcoat roughness (0: smooth, 1: rough)
    float anisotropy              = 0.0f;        ///< Anisotropy strength (0: isotropic, 1: fully anisotropic)
    glm::vec3 anisotropyDirection = {1,0,0};     ///< Anisotropy direction (tangent space)

    // Subsurface scattering (for skin, wax, etc.)
    float subsurface          = 0.0f;            ///< Subsurface scattering strength
    glm::vec3 subsurfaceColor = {1,1,1};         ///< Subsurface color tint

    // Height/Displacement
    float heightScale = 0.0f;                    ///< Height map scale for parallax/displacement mapping

    // Texture handles
    TextureHandle albedoTexture;                 ///< Albedo (base color) texture (RGBA)
    TextureHandle emissionTexture;               ///< Emission (self-illumination) texture (RGB or RGBA)
    TextureHandle roughnessTexture;              ///< Surface roughness texture (G or R channel)
    TextureHandle metallicTexture;               ///< Surface metalness texture (B or R channel)
    TextureHandle normalTexture;                 ///< Tangent-space normal map texture (RGB or RGBA)
    TextureHandle aoTexture;                     ///< Ambient occlusion texture (R channel)
    TextureHandle heightTexture;                 ///< Height/displacement map texture (R channel)
    TextureHandle clearcoatTexture;              ///< Clearcoat mask texture (R channel: clearcoat strength)
    TextureHandle transmissionTexture;           ///< Transmission mask texture (A or R channel: transparency/IOR)

    // Combined texture maps (common optimization)
    TextureHandle metallicRoughnessTexture;      ///< Combined metallic (R) + roughness (G) texture
    TextureHandle aoMetallicRoughnessTexture;    ///< Combined AO (R) + metallic (G) + roughness (B)

    uint64_t version = 1;
};

} // namespace Engine