#pragma once

// Common includes for editor panel implementation files.
// Each panel .cpp includes this instead of duplicating the list.

#include "editor_system.h"

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
#include "ecs/hierarchy_utils.h"
#include "resource/resource_manager.h"
#include "debug/statistics.h"
#include "system/visibility/visibility.h"
