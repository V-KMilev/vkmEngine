#pragma once

#include <cstdint>

#include "core/reflect.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Root of a screen-space UI layer.
 *
 * Descendant UIElements lay out against a reference height, scaled uniformly to the
 * live viewport. Several canvases draw in ascending sortOrder.
 */
struct UICanvas {
    /**
     * @brief How the canvas maps authored pixels to on-screen pixels.
     */
    enum class ScaleMode : uint8_t {
        Fixed,           ///< One authored pixel equals one screen pixel.
        ScaleWithHeight, ///< Scale uniformly by viewportHeight / referenceHeight.
        Count            ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
    };

    /// Authoring height in pixels; ScaleWithHeight scales against it.
    float     referenceHeight = 1080.0f;
    ScaleMode scaleMode       = ScaleMode::ScaleWithHeight;
    /// Draw order across canvases; higher draws on top.
    int32_t   sortOrder       = 0;
    /// Skip the canvas and its whole subtree when false.
    bool      visible         = true;
};

/**
 * @brief Whether a UICanvas sits strictly above @p id in the hierarchy.
 *
 * That is whether UISystem's layout walk reaches it at all: an element outside every
 * canvas, or on the canvas entity itself, is never drawn or hit-tested. Bounded by
 * HierarchyOperations::MAX_DEPTH.
 *
 * @param scene Scene to walk.
 * @param id Entity to answer for.
 * @return true when some ancestor of @p id carries a UICanvas.
 */
bool hasCanvasAncestor(const Scene& scene, EntityId id);

} // namespace Vkm::Engine

VKM_ENUM_NAMES(::Vkm::Engine::UICanvas::ScaleMode, "Fixed", "ScaleWithHeight")

VKM_REFLECT_BEGIN(::Vkm::Engine::UICanvas)
    VKM_F(referenceHeight)
    VKM_F(scaleMode)
    VKM_F(sortOrder)
    VKM_F(visible)
VKM_REFLECT_END()
