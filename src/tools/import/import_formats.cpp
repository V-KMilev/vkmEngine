#include "import/import_formats.h"

#include <algorithm>
#include <cctype>

namespace Vkm::Engine {

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return extension;
}

} // namespace Vkm::Engine
