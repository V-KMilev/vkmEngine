#pragma once

// Common includes for editor panel implementation files.
// Each panel .cpp includes its own header first, then this convenience header.
//
// Scope: the panel framework (state/context/widgets/style/icons/keybinds),
// the ImGui + glm prelude, and the ECS component types that essentially
// every panel touches. Heavier engine-side headers (debug stats, visibility,
// bounds utils) are NOT here - pull them in explicitly where used.

#include <imgui.h>
#include <glm/glm.hpp>

#include "system/render/editor_render_hooks.h"  // GpuTextureId, for imTexture below
#include <glm/gtc/type_ptr.hpp>

#include "core/system.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/component/render/lod.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "framework/editor_context.h"
#include "framework/editor_state.h"
#include "input/editor_keybinds.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "ui/editor_icons.h"
#include "ui/editor_style.h"
#include "ui/editor_widgets.h"

namespace Vkm::Engine {

/**
 * @brief Wrap a GL texture id as an ImGui ImTextureID for Image/ImageButton.
 *
 * @param glTextureId Raw OpenGL texture name to display.
 * @return The id reinterpreted as the opaque handle ImGui's image widgets expect.
 */
inline ImTextureID imTexture(GpuTextureId id) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(id));
}

} // namespace Vkm::Engine
