#include "net/wire/project_path.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace Vkm::Engine {

namespace {

constexpr uint32_t PATH_BITS = 8;  ///< Bits for a project path's length. Enough for NET_PROJECT_PATH_MAX.

static_assert(NET_PROJECT_PATH_MAX < (1u << PATH_BITS), "a path has to be able to say how long it is");
static_assert(PATH_BITS <= 8, "NET_SPAWN_MAX_BYTES counts the length as one byte");

constexpr std::string_view JSON_EXTENSION = ".json";  ///< What a prefab or scene file is named with.


/**
 * @brief Whether a path component names a Windows device rather than a file.
 *
 * In any directory and with any extension: "prefabs/con.json" opens the
 * console on Windows and blocks.
 *
 * @param component One component of a '/'-separated path.
 * @return True for a reserved device name, in any case, with or without an extension.
 */
bool isDeviceName(std::string_view component) {
    const std::string_view stem = component.substr(0, component.find('.'));
    if (stem.size() != 3 && stem.size() != 4) return false;

    std::string lower(stem);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") return true;
    return lower.size() == 4 && (lower.compare(0, 3, "com") == 0 || lower.compare(0, 3, "lpt") == 0)
        && lower[3] >= '0' && lower[3] <= '9';
}

} // namespace

bool isProjectJsonPath(std::string_view path) {
    if (path.empty() || path.size() > NET_PROJECT_PATH_MAX) return false;
    if (path.size() <= JSON_EXTENSION.size()
        || path.substr(path.size() - JSON_EXTENSION.size()) != JSON_EXTENSION) {
        return false;
    }
    // A root, a drive, a share or a stream; and a character no file name holds.
    if (path.front() == '/') return false;
    for (const char c : path) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\' || c == ':' || byte < 0x20 || byte == 0x7F) return false;
    }
    size_t begin = 0;
    while (begin <= path.size()) {
        const size_t end = std::min(path.find('/', begin), path.size());
        const std::string_view component = path.substr(begin, end - begin);
        if (component == ".." || isDeviceName(component)) return false;
        begin = end + 1;
    }
    return true;
}

void writeProjectPath(std::string_view path, Network::BitWriter& out) {
    const std::string_view said = isProjectJsonPath(path) ? path : std::string_view{};
    out.bits(static_cast<uint32_t>(said.size()), PATH_BITS);
    for (const char c : said) out.bits(static_cast<uint8_t>(c), 8);
}

std::string readProjectPath(Network::BitReader& in) {
    // The width bounds the cost of reading whatever a sender claims.
    std::string path(in.bits(PATH_BITS), '\0');
    for (char& c : path) c = static_cast<char>(in.bits(8));
    return path;
}

} // namespace Vkm::Engine
