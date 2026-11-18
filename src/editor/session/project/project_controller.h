#pragma once

#include <cstdint>
#include <string>

namespace Vkm::Engine {

struct EditorContext;
class ScriptModule;

/// Opening a project: one ordered sequence, reached through the unsaved-changes guard.
namespace ProjectController {

/**
 * @brief Who asked: Startup finding no project says nothing; Requested reports a path that is not one.
 */
enum class OpenKind : uint8_t { Startup, Requested };

/**
 * @brief Root the editor in the project at @p projectRoot.
 *
 * In order, each step needing the last: write the open project's settings, read the new one's, tear
 * the old scene down, re-root paths, load the asset library and editor settings, swap the gameplay
 * module, boot the scene. A directory with no project.json is refused before any teardown; an entry
 * scene that will not load opens the default scene.
 *
 * @param ec           Editor context to root; its state records the project as open.
 * @param scriptModule Loaded from the project's bin/.
 * @param projectRoot  Project directory, or any path inside it.
 * @param kind         See OpenKind.
 * @return True when the directory was a project and the open ran.
 */
bool open(EditorContext& ec, ScriptModule& scriptModule, const std::string& projectRoot, OpenKind kind);

} // namespace ProjectController

} // namespace Vkm::Engine
