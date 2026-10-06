#include "resource/asset/animation_clip_asset.h"

#include <cmath>

namespace Vkm::Engine {

namespace {

// [first, first + count) within `size`; subtracting so the bound cannot wrap.
bool channelInRange(const ClipChannel& channel, size_t size) {
    return channel.count <= size && channel.first <= size - channel.count;
}

} // namespace

std::string findClipFault(const AnimationClipAsset& clip) {
    // advanceHead divides by the duration; a NaN would spread through every pose.
    if (!std::isfinite(clip.duration) || clip.duration < 0.0f) {
        return "its duration " + std::to_string(clip.duration) + " is not a length of time";
    }
    if (clip.positionTimes.size() != clip.positions.size()
        || clip.rotationTimes.size() != clip.rotations.size()
        || clip.scaleTimes.size() != clip.scales.size()) {
        return "its key times and values disagree in length";
    }
    for (size_t i = 0; i < clip.bones.size(); ++i) {
        const ClipBone& bone = clip.bones[i];
        if (!channelInRange(bone.position, clip.positions.size())
            || !channelInRange(bone.rotation, clip.rotations.size())
            || !channelInRange(bone.scale, clip.scales.size())) {
            return "bone " + std::to_string(i) + " names keys past the "
                + std::to_string(clip.positions.size()) + "/"
                + std::to_string(clip.rotations.size()) + "/"
                + std::to_string(clip.scales.size()) + " it has";
        }
    }
    // A marker outside the timeline never fires at its instant.
    for (const ClipMarker& marker : clip.markers) {
        if (std::isfinite(marker.time) && marker.time >= 0.0f && marker.time <= clip.duration) continue;
        return "marker '" + marker.name + "' at " + std::to_string(marker.time)
            + " is outside its " + std::to_string(clip.duration) + " seconds";
    }
    return {};
}

} // namespace Vkm::Engine
