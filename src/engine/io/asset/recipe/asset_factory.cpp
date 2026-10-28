#include "io/asset/asset_factory.h"

namespace Vkm::Engine {

AssetFactory& assetFactory() {
    static AssetFactory s_factory;
    return s_factory;
}

} // namespace Vkm::Engine
