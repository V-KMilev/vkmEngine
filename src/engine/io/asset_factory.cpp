#include "io/asset_factory.h"

namespace Engine {

AssetFactory& assetFactory() {
    static AssetFactory f;
    return f;
}

} // namespace Engine
