#include "resource/asset/audio_clip_asset.h"

#include <utility>

namespace Vkm::Engine {

ClipSamples::ClipSamples(std::vector<int16_t> samples)
    : m_samples(std::make_shared<const std::vector<int16_t>>(std::move(samples)))
{
}

} // namespace Vkm::Engine
