#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace Vkm::Engine {

/// Image files the importers read, by lowercased extension.
inline constexpr std::array<std::string_view, 5> IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".tga", ".bmp"};
/// Read by parseModel: glTF itself, the rest through ufbx.
inline constexpr std::array<std::string_view, 4> MODEL_EXTENSIONS = {".gltf", ".glb", ".obj", ".fbx"};
/// Read by the sound loader (miniaudio's decoders).
inline constexpr std::array<std::string_view, 3> AUDIO_EXTENSIONS = {".wav", ".mp3", ".flac"};

/**
 * @brief @p path's extension, lowercased, as the lists above spell it.
 *
 * @param path A file name or path; only its last component is read, so it need not exist.
 * @return Its extension with the dot, e.g. ".png"; empty for none.
 */
std::string lowercaseExtension(const std::filesystem::path& path);

} // namespace Vkm::Engine
