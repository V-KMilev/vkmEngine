# Animation System

The AnimationSystem plays keyframe-based property animations onto entity transforms.
It runs in the Simulation stage and reads `simDeltaTime`, so pause / time-scale /
single-step apply uniformly.

## Key files

- `src/engine/system/animation/animation_system.h` - AnimationSystem
- `src/engine/system/animation/animation_track.h` - AnimationTrack<T> (keyframe storage lives here)
- `src/engine/core/math/easing.h` - easing functions (interpolation curves)
- `src/engine/ecs/component/animation.h` - Animation component

## Update model

The system makes a single pass over **every** entity with an `Animation` component
(it does not filter by visibility). For each playing animation it:

1. Advances `time` by the frame's simulation delta scaled by `speed`.
2. Handles the end of the timeline - wraps when `looping`, otherwise clamps and
   stops.
3. Samples each track and writes `Transform.position` / `.rotation` / `.scale`.
4. Marks the entity's subtree dirty so `HierarchySystem` recomputes the affected
   `WorldTransform`s downstream.

Because it applies to all animated entities (not just visible ones), off-screen
animation stays in sync; the cost is bounded by the number of *animated* entities,
not the scene size.

## Animation component

```cpp
struct Animation {
    AnimationTrack<glm::vec3> positionTrack;
    AnimationTrack<glm::quat> rotationTrack;
    AnimationTrack<glm::vec3> scaleTrack;

    float duration = 0.0f;   // cached effective duration; stale until updateDuration()
    float length   = 0.0f;   // explicit minimum length (0 = auto from last keyframe)
    float time     = 0.0f;   // current playback time
    float speed    = 1.0f;   // playback multiplier
    bool  playing  = false;
    bool  looping  = true;

    void updateDuration();   // = max(each track's last keyframe, length)
};
```

`duration` is a **cache**: call `updateDuration()` after editing keyframes, tracks,
or `length`, or it will be stale. `length` lets you hold an animation open past its
last keyframe (e.g. a pause at the end of a loop); with `length == 0` the duration is
just the latest keyframe across the three tracks. Each track is independent - an
entity can animate position only, or any combination.

## AnimationTrack<T>

A time-sorted keyframe sequence with one easing function:

```cpp
AnimationTrack<glm::vec3> track;
track.setEasing(Easing::easeInOutSine);
track.addKeyframe(0.0f, {0, 0, 0});
track.addKeyframe(2.0f, {0, 5, 0});

glm::vec3 value = track.getValue(1.0f);  // eased, interpolated
```

`getValue(time)`:

1. Clamp `time` to the `[first, last]` keyframe range.
2. Binary-search the enclosing interval.
3. Compute the normalized `t` within it and apply the easing function.
4. Interpolate: `glm::slerp` for `glm::quat`, `glm::mix` otherwise - selected with
   `if constexpr (is_same_v<T, glm::quat>)`.

`addKeyframe()` inserts and keeps the sequence sorted by time.

## Keyframe storage

There is no `Keyframe<T>` struct. An `AnimationTrack<T>` stores its keyframes as
two parallel, time-sorted vectors:

```cpp
std::vector<float> m_times;   // keyframe times, ascending
std::vector<T>     m_values;  // value at each time (vec3 or quat)
```

`addKeyframe(time, value)` inserts into both at the position that keeps `m_times`
sorted, and `getValue` binary-searches `m_times` for the enclosing interval. The
parallel-array layout keeps the time lookup cache-friendly and avoids an
array-of-structs.

## Easing functions

`using EasingFunction = float(*)(float)` - takes normalized `t` in `[0, 1]`, returns
the eased value. 31 functions (`linear` plus ten In/Out/InOut families):

| Family | In | Out | InOut |
|--------|----|-----|-------|
| Linear | `linear` | - | - |
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
