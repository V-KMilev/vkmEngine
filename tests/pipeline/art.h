#pragma once

// Source art written in code, as a modeling or paint program writes it: images in each format the
// importers read, and one box as glTF, GLB, OBJ and FBX, so a test owns every byte it imports.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_precision.hpp>
#include <nlohmann/json.hpp>

namespace Vkm::Test {

/**
 * @brief Write @p pixels as the image format @p path's extension names: png, jpg, tga or bmp.
 *
 * @param path     Where; its folders are made.
 * @param width    Texels across.
 * @param height   Texels down, the first row the top.
 * @param channels 1 gray, 2 gray and alpha, 3 RGB, 4 RGBA.
 * @param pixels   width * height * channels bytes.
 */
void writeImage(
    const std::filesystem::path& path,
    int width,
    int height,
    int channels,
    const std::vector<uint8_t>& pixels
);

/// @p pixels as the bytes of a PNG file, as writeImage takes them.
std::string pngBytes(int width, int height, int channels, const std::vector<uint8_t>& pixels);

/**
 * @brief Write a 16-bit PCM wav.
 *
 * @param path     Where.
 * @param rate     Frames a second.
 * @param channels Interleaved channels.
 * @param samples  Every channel's samples, frame by frame.
 */
void writeWav(
    const std::filesystem::path& path,
    uint32_t rate,
    uint16_t channels,
    const std::vector<int16_t>& samples
);

/// One face of a solid: four corners counter-clockwise seen from outside, its normal and its UVs
/// with V running up the image, as OBJ and FBX store them.
struct Face {
    glm::vec3 corners[4];
    glm::vec3 normal;
    glm::vec2 uvs[4];
};

/// A box from @p low to @p high in meters, Y up, its six faces facing out.
std::vector<Face> boxFaces(const glm::vec3& low, const glm::vec3& high);

/// The faces as an OBJ in meters, Y up, as the format assumes.
std::string objText(const std::vector<Face>& faces);

/// The faces as an ASCII FBX in centimeters with Z up, as a Z-up tool exports one.
std::string fbxText(const std::vector<Face>& faces);

/**
 * @brief A glTF document and its one buffer, built a part at a time.
 */
struct Gltf {
    nlohmann::json doc;
    std::string    bin;

    Gltf();

    /// A buffer view over @p bytes, 4-aligned in the buffer; its index.
    int view(const void* bytes, size_t size);

    /// An accessor over a view of its own; its index.
    int accessor(const void* bytes, size_t count, int componentType, const char* type, size_t elementBytes);

    int floats(const std::vector<float>& values);
    int vec2s(const std::vector<glm::vec2>& values);
    int vec3s(const std::vector<glm::vec3>& values);
    int vec4s(const std::vector<glm::vec4>& values);
    int mat4s(const std::vector<glm::mat4>& values);
    int indices(const std::vector<uint32_t>& values);
    int joints(const std::vector<glm::u8vec4>& values);

    /// Append @p value to the document's array @p key; its index there.
    int add(const char* key, nlohmann::json value);

    /// The document with its buffer inline as a data URI: a .gltf.
    std::string text() const;

    /// The binary container, its buffer in its BIN chunk: a .glb.
    std::string binary() const;
};

/**
 * @brief The faces as one glTF mesh of one primitive, its UVs with V down as glTF stores them.
 *
 * @param gltf     Receives the accessors and the mesh.
 * @param faces    The solid.
 * @param material The primitive's material index; -1 for none.
 * @return The mesh's index.
 */
int addMesh(Gltf& gltf, const std::vector<Face>& faces, int material = -1);

/**
 * @brief A skinned glTF: a box two meters tall bound to a hip and a spine above it, the top
 *        corners three quarters on the spine, and one clip, "sway", over a second: the spine
 *        turning a quarter about Z, the hip rising half a meter at 0.5 s and back.
 *
 * @return The .gltf text.
 */
std::string riggedGltf();

} // namespace Vkm::Test
