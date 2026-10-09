#include "system/ui/screen_view.h"

#include <glm/gtc/constants.hpp>

#include "core/host_chrome.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/scene.h"
#include "platform/window/window_manager.h"

namespace Vkm::Engine {

ScreenView ScreenView::of(const Scene& scene, const WindowManager& window, const HostChrome& chrome) {
    ScreenView view;
    const HostChrome::ViewportRect rect = chrome.viewport(window);
    const EntityId                 eye  = findActiveCamera(scene);
    const Transform*               pose = scene.tryGet<Transform>(eye);
    const Camera*                  lens = scene.tryGet<Camera>(eye);
    if (!pose || !lens || rect.width == 0 || rect.height == 0) return view;

    // Its world pose, a rig's included, and for an auto-aspect lens the view's own aspect.
    Transform world;
    world.position = resolvedWorldPosition(scene, eye, *pose);
    world.rotation = resolvedWorldRotation(scene, eye, *pose);
    view.m_origin         = {static_cast<float>(rect.x), static_cast<float>(rect.y)};
    view.m_size           = {static_cast<float>(rect.width), static_cast<float>(rect.height)};
    const glm::mat4 projection = Camera::computeProjection(*lens, view.m_size.x / view.m_size.y);
    view.m_viewProjection = projection * Transform::computeView(world);
    view.m_pointerScale   = window.framebufferScale();
    view.m_valid          = true;
    return view;
}

std::optional<Math::Ray> ScreenView::rayThrough(const glm::vec2& windowPoint) const {
    if (!m_valid) return std::nullopt;
    const glm::vec2 inView = windowPoint * m_pointerScale - m_origin;
    const bool off = inView.x < 0.0f || inView.y < 0.0f || inView.x >= m_size.x || inView.y >= m_size.y;
    if (off) return std::nullopt;
    const glm::vec2 ndc = {inView.x / m_size.x * 2.0f - 1.0f, 1.0f - inView.y / m_size.y * 2.0f};
    return Math::rayThroughNdc(glm::inverse(m_viewProjection), ndc);
}

std::optional<glm::vec2> ScreenView::toCanvas(const glm::vec3& world, const UICanvas& canvas) const {
    if (!m_valid) return std::nullopt;
    const glm::vec4 clip = m_viewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= glm::epsilon<float>()) return std::nullopt;
    const glm::vec2 ndc   = glm::vec2(clip) / clip.w;
    const glm::vec2 pixel = {(ndc.x * 0.5f + 0.5f) * m_size.x, (0.5f - ndc.y * 0.5f) * m_size.y};
    return pixel / UICanvas::scaleIn(canvas, m_size.y);
}

} // namespace Vkm::Engine
