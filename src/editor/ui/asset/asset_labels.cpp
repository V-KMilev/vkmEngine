#include "ui/asset/asset_labels.h"

namespace Vkm::Engine {

std::string assetFileStem(const std::string& name) {
    const std::string file = name.substr(0, name.find(':'));
    if (file.find_first_of("/.") == std::string::npos) return {};
    const size_t slash = file.find_last_of('/');
    std::string  stem  = slash == std::string::npos ? file : file.substr(slash + 1);
    if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos && dot > 0) stem.resize(dot);
    return stem;
}

std::string assetShortName(const std::string& name) {
    const std::string stem = assetFileStem(name);
    return stem.empty() ? name.substr(name.rfind(':') + 1) : stem;
}

} // namespace Vkm::Engine
