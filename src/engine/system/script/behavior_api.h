#pragma once

// What a gameplay behavior commonly reaches, behind one include, for project behavior
// headers; engine code includes what it uses, by name.

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "resource/asset_ref.h"
#include "resource/resource_manager.h"
#include "system/audio/audio_events.h"
#include "system/physics/physics_events.h"
#include "system/script/behavior_registry.h"
#include "system/script/reflected_behavior.h"
#include "system/script/script_component.h"
