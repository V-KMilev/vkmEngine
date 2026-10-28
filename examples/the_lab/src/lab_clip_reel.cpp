#include "lab_clip_reel.h"

#include <iterator>

#include "ecs/component/animation/animator.h"
#include "ecs/scene.h"
#include "resource/resource_manager.h"

namespace Lab {

void LabClipReel::onStart() {
    m_animator = findPartWith<Animator>();
}

void LabClipReel::onUpdate(float dt) {
    m_left -= dt;
    if (m_left > 0.0f) return;
    m_left = holdSeconds;
    playNext();
}

void LabClipReel::playNext() {
    Animator* animator = scene().tryGet<Animator>(m_animator);
    if (!animator) return;

    const AssetRef<AnimationClipAsset>* clips[] = {&idleClip, &walkClip, &runClip, &jumpClip};
    const int count = static_cast<int>(std::size(clips));
    for (int step = 1; step <= count; ++step) {
        const int index = (m_playing + step) % count;
        const AnimationClipHandle clip = resources().find(*clips[index]);
        if (!clip) continue;
        const bool looping = true;
        Animator::crossFadeTo(*animator, clip, fadeSeconds, looping);
        m_playing = index;
        return;
    }
}

} // namespace Lab
