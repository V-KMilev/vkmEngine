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
#include <glm/gtc/type_ptr.hpp>

#include "core/system.h"   // FrameContext
#include "ecs/component/animation.h"
#include "ecs/component/camera.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/light.h"
#include "ecs/component/mesh.h"
#include "ecs/component/name.h"
#include "ecs/component/transform.h"
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
