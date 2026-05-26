# Animation System

The AnimationSystem processes keyframe-based property animations on entity transforms.

## Key Files

- `src/engine/system/animation/animation_system.h`: AnimationSystem
- `src/engine/system/animation/animation_track.h`: AnimationTrack<T>
- `src/engine/system/animation/keyframe.h`: Keyframe<T>
- `src/engine/system/animation/easing.h`: Easing functions
- `src/engine/ecs/component/animation.h`: Animation component

## Two-Phase Update

The AnimationSystem runs in two phases each frame:

### Phase 1: Advance All Timelines

Iterates **all** entities with an `Animation` component:
- Skip if not playing
- Advance `time += deltaTime * speed`
- Handle looping: `fmod(time, duration)` or pause at end

### Phase 2: Apply to Visible Only

Iterates only **visible** entities (from `FrameContext.visibility`):
- Apply position track to `Transform.position`
- Apply rotation track to `Transform.rotation`
- Apply scale track to `Transform.scale`

This ensures all animations stay in sync (phase 1) while avoiding wasted transform writes for off-screen entities (phase 2).

## Animation Component

```cpp
struct Animation {
    AnimationTrack<glm::vec3> positionTrack;
    AnimationTrack<glm::quat> rotationTrack;
    AnimationTrack<glm::vec3> scaleTrack;

    float duration = 0.0f;
    float time     = 0.0f;
    float speed    = 1.0f;
    bool playing   = false;
    bool looping   = true;
};
```

Each track is independent: an entity can animate position without rotation, or any combination.

## AnimationTrack<T>

A sequence of keyframes with an easing function:

```cpp
AnimationTrack<glm::vec3> track;
track.setEasing(Easing::easeInOutSine);
track.addKeyframe(0.0f, {0, 0, 0});   // time=0, position=(0,0,0)
track.addKeyframe(2.0f, {0, 5, 0});   // time=2, position=(0,5,0)

glm::vec3 value = track.getValue(1.0f);  // interpolated at t=1
```

`getValue(time)`:
1. Clamp time to [first, last] keyframe
2. Binary search for enclosing interval
3. Compute normalized `t` within interval
4. Apply easing function to `t`
5. Interpolate: `glm::slerp` for quaternions, `glm::mix` for vec3 (via `if constexpr`)

Keyframes are kept sorted by time. `addKeyframe()` inserts and re-sorts.

## Keyframe<T>

```cpp
template<typename T>
struct Keyframe {
    float time;
    T value;
};
```

Type aliases: `PositionKeyframe` (vec3), `RotationKeyframe` (quat), `ScaleKeyframe` (vec3).

## Easing Functions

`using EasingFunction = float(*)(float)`: takes normalized `t` in [0,1], returns eased value.

31+ functions available:

| Category | In | Out | InOut |
|----------|----|-----|-------|
| Linear | `linear` | n/a | n/a |
| Quadratic | `easeInQuad` | `easeOutQuad` | `easeInOutQuad` |
| Cubic | `easeInCubic` | `easeOutCubic` | `easeInOutCubic` |
| Quartic | `easeInQuart` | `easeOutQuart` | `easeInOutQuart` |
| Quintic | `easeInQuint` | `easeOutQuint` | `easeInOutQuint` |
| Sine | `easeInSine` | `easeOutSine` | `easeInOutSine` |
| Exponential | `easeInExpo` | `easeOutExpo` | `easeInOutExpo` |
| Circular | `easeInCirc` | `easeOutCirc` | `easeInOutCirc` |
| Back | `easeInBack` | `easeOutBack` | `easeInOutBack` |
| Elastic | `easeInElastic` | `easeOutElastic` | `easeInOutElastic` |
| Bounce | `easeInBounce` | `easeOutBounce` | `easeInOutBounce` |

All implemented using GLM math functions.
