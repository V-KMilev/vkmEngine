#include "system/script/behavior_base.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "base/math/axes.h"
#include "base/math/random.h"
#include "base/math/rotation.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "net/net_role.h"
#include "resource/resource_manager.h"
#include "system/audio/audio_events.h"
#include "system/script/script_component.h"
#include "core/clock.h"

namespace Vkm::Engine {

namespace {

// Below this squared length a leveled axis has no direction left: the view looks straight along up.
constexpr float LEVEL_EPSILON = 1e-6f;

// @p direction laid flat on the ground and made unit, or zero when it points straight up or down.
glm::vec3 leveled(const glm::vec3& direction) {
    const glm::vec3 flat = direction - Math::WORLD_UP * glm::dot(direction, Math::WORLD_UP);
    const float sq = glm::dot(flat, flat);
    return sq > LEVEL_EPSILON ? flat / std::sqrt(sq) : glm::vec3(0.0f);
}

} // namespace

namespace detail {

namespace {

BehaviorBase* namedOn(const ScriptComponent& script, std::string_view typeName) {
    for (const std::unique_ptr<BehaviorBase>& behavior : script.behaviors) {
        if (behavior && typeName == behavior->typeName()) return behavior.get();
    }
    return nullptr;
}

} // namespace

BehaviorBase* findBehaviorNamed(Scene& scene, EntityId entity, std::string_view typeName) {
    const ScriptComponent* script = scene.tryGet<ScriptComponent>(entity);
    return script ? namedOn(*script, typeName) : nullptr;
}

EntityId lowestWithBehaviorNamed(Scene& scene, std::string_view typeName) {
    return findLowestSlot<ScriptComponent>(scene, [&](const ScriptComponent& script) {
        return namedOn(script, typeName) != nullptr;
    });
}

namespace {

thread_local std::vector<std::pair<EntityId, BehaviorBase*>> t_carrying;
    ///< What forEachBehaviorNamed meets, kept per thread for its capacity.

} // namespace

void forEachBehaviorNamed(
    Scene& scene,
    std::string_view typeName,
    const std::function<void(EntityId, BehaviorBase&)>& fn
) {
    // Borrowed by swap, so a walk fn starts takes a fresh list instead of this one.
    std::vector<std::pair<EntityId, BehaviorBase*>> carrying;
    carrying.swap(t_carrying);
    scene.forEach<ScriptComponent>([&](EntityId entity, ScriptComponent& script) {
        BehaviorBase* behavior = namedOn(script, typeName);
        if (behavior) carrying.emplace_back(entity, behavior);
    });
    // The storage's order is not one two ends of a session share; the slots' is.
    std::sort(carrying.begin(), carrying.end(), [](const auto& a, const auto& b) {
        return a.first.slot() < b.first.slot();
    });
    for (const auto& [entity, behavior] : carrying) fn(entity, *behavior);
    carrying.clear();
    t_carrying.swap(carrying);
}

} // namespace detail

glm::vec3 BehaviorBase::worldPosition() const {
    if (!tryGet<Transform>()) return glm::vec3(0.0f);
    return glm::vec3(HierarchyOperations::computeWorldMatrix(frame().scene, m_entity)[3]);
}

glm::quat BehaviorBase::worldRotation() const {
    if (!tryGet<Transform>()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return Math::worldRotationOf(HierarchyOperations::computeWorldMatrix(frame().scene, m_entity));
}

std::optional<RayHit> BehaviorBase::cast(
    const glm::vec3& from,
    const glm::vec3& direction,
    float distance,
    QueryFilter filter
) const {
    filter.ignore = m_entity;
    RayHit hit;
    if (!raycast(frame().scene, from, direction, distance, hit, filter)) return std::nullopt;
    return hit;
}

void BehaviorBase::overlapSphere(
    const glm::vec3& center,
    float radius,
    std::vector<EntityId>& found,
    QueryFilter filter
) const {
    filter.ignore = m_entity;
    Vkm::Engine::overlapSphere(frame().scene, center, radius, found, filter);
}

bool BehaviorBase::pressed(std::string_view action) const {
    const InputMap& map = frame().input;
    return m_ctx->ticking ? map.pressed(command(), action) : map.pressed(action);
}

bool BehaviorBase::held(std::string_view action) const {
    const InputMap& map = frame().input;
    return m_ctx->ticking ? map.held(command(), action) : map.held(action);
}

bool BehaviorBase::released(std::string_view action) const {
    const InputMap& map = frame().input;
    return m_ctx->ticking ? map.released(command(), action) : map.released(action);
}

float BehaviorBase::axis(std::string_view action) const {
    const InputMap& map = frame().input;
    return m_ctx->ticking ? map.axis(command(), action) : map.axis(action);
}

glm::vec3 BehaviorBase::alongView(float right, float forward) const {
    const glm::quat& view = command().view;
    glm::vec3 ahead = leveled(Math::computeForward(view));
    if (ahead == glm::vec3(0.0f)) ahead = leveled(Math::computeUp(view));

    const glm::vec3 move   = leveled(Math::computeRight(view)) * right + ahead * forward;
    const float     length = glm::length(move);
    return length > 1.0f ? move / length : move;
}

bool BehaviorBase::isAuthority() const {
    return Vkm::Engine::isAuthority(frame().net.role());
}

double BehaviorBase::worldTime() const {
    return Vkm::Engine::worldTime(frame().net, frame().clock);
}

EntityId BehaviorBase::spawn(std::string_view name, EntityId parent) {
    Scene& world = frame().scene;
    const EntityId entity = world.createEntity();
    Name named;
    name.copy(named.value, std::min(name.size(), sizeof(named.value) - 1));
    world.add(entity, named);
    if (parent) HierarchyOperations::setParent(world, entity, parent);
    return entity;
}

EntityId BehaviorBase::instantiate(const PrefabRef& prefab, const Transform& at) {
    return Prefab::instantiate(frame().scene, frame().resources, prefab.path, at);
}

EntityId BehaviorBase::instantiateShared(const PrefabRef& prefab, const Transform& at) {
    return frame().net.spawn(frame().scene, frame().resources, prefab.path, at);
}

void BehaviorBase::sendSound(
    const AssetRef<AudioClipAsset>& clip,
    const glm::vec3& at,
    float volume,
    float spread,
    EntityId from
) {
    if (isReplaying()) return;  // a replay re-runs a moment already presented
    const AudioClipHandle sound = frame().resources.find(clip);
    if (!sound) return;
    const float    louder  = spread > 0.0f ? 1.0f + Math::Random::range(-spread, spread) : 1.0f;
    PlaySoundEvent request = PlaySoundEvent::at(sound, at, volume * louder);
    request.from = from;
    if (spread > 0.0f) request.params.pitch = 1.0f + Math::Random::range(-spread, spread);
    frame().events.emit(request);
}

void BehaviorBase::destroyAfter(EntityId target, float seconds) {
    if (isReplaying()) return;  // the live tick asked already, and asked again its clock would restart
    std::vector<TimedDestroy>& timed = m_ctx->timedDestroy;
    const auto held = std::find_if(timed.begin(), timed.end(), [&](const TimedDestroy& entry) {
        return entry.entity == target;
    });
    if (held != timed.end()) held->seconds = seconds;
    else timed.push_back({target, seconds});
}

bool BehaviorBase::playAnimation(
    const AssetRef<AnimationClipAsset>& clip,
    float fadeSeconds,
    AnimationEnd end
) {
    if (isReplaying()) return false;  // it would play from a past state, then the live tick back
    Animator* animator = frame().scene.tryGet<Animator>(findPartWith<Animator>());
    const AnimationClipHandle handle = frame().resources.find(clip);
    if (!animator || !handle) return false;
    Animator::crossFadeTo(*animator, handle, fadeSeconds, end == AnimationEnd::Loop);
    return true;
}

EntityId BehaviorBase::findNamed(std::string_view name) const {
    return findLowestSlot<Name>(frame().scene, [&](const Name& named) { return name == named.value; });
}

ScreenView BehaviorBase::screen() const {
    if (!frame().window.isOpen()) return {};
    return ScreenView::of(frame());
}

std::optional<Math::Ray> BehaviorBase::pointerRay() const {
    return screen().rayThrough(frame().input.pointer());
}

} // namespace Vkm::Engine
