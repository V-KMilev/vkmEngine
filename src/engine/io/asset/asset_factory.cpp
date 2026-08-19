#include "io/asset/asset_factory.h"

namespace Vkm::Engine {

AssetFactory& assetFactory() {
    static AssetFactory f;
    return f;
}

} // namespace Vkm::Engine
