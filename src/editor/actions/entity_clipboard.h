#pragma once

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

namespace EditorActions {

/**
 * @brief Take the selection's roots, with their subtrees, for Paste.
 *
 * @param scene     Holds the selection.
 * @param resources Names the copies' assets.
 * @param state     Its selection is taken; its clipboard replaced.
 */
void copySelection(const Scene& scene, const ResourceManager& resources, EditorState& state);

/**
 * @brief Copy the selection, then delete it as one undo step.
 *
 * @param scene     Holds the selection, and loses it.
 * @param resources See copySelection.
 * @param state     See copySelection; takes the step.
 */
void cutSelection(Scene& scene, const ResourceManager& resources, EditorState& state);

/**
 * @brief Place what was copied as one undo step, and select it.
 *
 * In the world it was taken from, each copy stands where it stood, under its parent while copyParent
 * keeps it; in another, at the root. References outside what was copied are dropped.
 *
 * @param scene     Receives the copies.
 * @param resources Resolves the copies' asset names, and takes the assets they carry that it lacks.
 * @param state     Takes the step, the selection, the dirty flag and the toasts.
 */
void paste(Scene& scene, ResourceManager& resources, EditorState& state);

} // namespace EditorActions

} // namespace Vkm::Engine
