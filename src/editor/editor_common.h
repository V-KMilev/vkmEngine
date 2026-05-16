#pragma once

// Common includes for editor panel implementation files.
// Each panel .cpp includes its own header first, then this convenience header.

#include "editor_state.h"
#include "editor_widgets.h"
#include "editor_keybinds.h"
#include "editor_style.h"
#include "editor_icons.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>

#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/mesh.h"
#include "ecs/component/light.h"
#include "ecs/component/camera.h"
#include "ecs/component/animation.h"
#include "ecs/component/hierarchy.h"
#include "ecs/component/name.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "resource/resource_manager.h"
#include "resource/material_asset.h"
#include "debug/statistics.h"
#include "system/visibility/visibility.h"
#include "system/visibility/bounds_utils.h"
