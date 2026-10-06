#pragma once

#include <cstdint>
#include <string>

namespace Vkm::Engine {

struct EditorContext;
class ScriptModule;
class SceneIOController;

/**
 * @brief Opening a project, the one ordered sequence for doing so.
 *
 * Requests pass the unsaved-changes guard before reaching here.
 */
namespace ProjectController {

/**
 * @brief Whether somebody asked for this project.
 *
 * Startup finding no project says nothing; Requested reports a path that is not
 * one. Whether an outgoing project is torn down is EditorState::projectOpen's call.
 */
enum class OpenKind : uint8_t { Startup, Requested };

/**
 * @brief Root the editor in the project at @p projectRoot.
 *
 * In order, each step depending on the one before: write the outgoing project's
 * settings, re-root paths, tear the old scene down, load the asset library and
 * editor settings, swap the gameplay module, boot the scene. The write and
 * teardown run only when a project is open. A directory with no project.json is
 * refused before any teardown, leaving EditorState::projectOpen as it was; an
 * entry scene that will not load opens the default scene.
 *
 * @param ec           Editor context to root; its state records the project as open.
 * @param scriptModule Loaded from the project's bin/.
 * @param sceneIO      Owns the teardown; adopts the booted scene path for Save.
 * @param projectRoot  Project directory, or any path inside it.
 * @param kind         See OpenKind.
 * @return True when the directory was a project and the open ran.
 */
bool open(
    EditorContext& ec,
    ScriptModule& scriptModule,
    SceneIOController& sceneIO,
    const std::string& projectRoot,
    OpenKind kind
);

} // namespace ProjectController

} // namespace Vkm::Engine
