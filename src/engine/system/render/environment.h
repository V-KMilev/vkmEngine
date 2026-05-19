#pragma once

#include "system/render/render_view.h"  // EnvironmentConfig

namespace Engine {

class Scene;

/**
 * @brief Access the scene's singleton Environment.
 *
 * Rendering/post settings live as an EnvironmentConfig component on a single
 * named "Environment" entity, so the editor can select and inspect it like
 * any other object and it round-trips with the scene. These helpers find that
 * entity (there is conceptually only one).
 */

/// The scene's environment config, creating the singleton entity (with a
/// Name) on first call. Safe to call every frame; only creates once.
EnvironmentConfig& sceneEnvironment(Scene& scene);

/// Non-creating lookup. nullptr if the Environment entity does not exist yet.
EnvironmentConfig* tryGetSceneEnvironment(Scene& scene);

} // namespace Engine
