#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

/**
 * @brief Importing a file of each kind the editor reads, from its pickers and from a file dropped
 *        on its window.
 */
namespace FileImport {

/**
 * @brief Import a model and place it in the scene, as Import Model does: an undo step, and the
 *        new root selected.
 *
 * @param path      The model file, inside the project.
 * @param scene     Receives the model.
 * @param resources Receives its meshes, materials, rig and clips.
 * @param state     Receives the undo step, the selection and what it says.
 * @return Whether the file was understood.
 */
bool importModel(const std::string& path, Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Import an image as a color texture; one the project already holds is left as it is.
 *
 * @param path      The image, inside the project.
 * @param resources Receives the texture.
 * @param state     Told what came of it.
 * @return Whether the project now holds it.
 */
bool importTexture(const std::string& path, ResourceManager& resources, EditorState& state);

/**
 * @brief Import a sound; one the project already holds is left as it is.
 *
 * @param path      The sound, inside the project.
 * @param resources Receives the clip.
 * @param state     Told what came of it.
 * @return Whether the project now holds it.
 */
bool importSound(const std::string& path, ResourceManager& resources, EditorState& state);

/**
 * @brief Where @p file is imported from: itself inside the project's assets, else a copy made
 *        there - a model in a folder of its own under `assets/models/` with the files it names
 *        beside it, an image under `assets/textures/`, a sound under `assets/audio/`.
 *
 * A file of the name already there is used when it holds the same bytes; otherwise the copy takes
 * a free name.
 *
 * @param file       A file anywhere.
 * @param[out] error Why nothing came of it, or what was left behind.
 * @return The file in the project; empty when it could not be brought in.
 */
std::filesystem::path intoProject(const std::filesystem::path& file, std::string& error);

/**
 * @brief Import files dropped on the editor's window, each brought into the project and imported
 *        as its kind's Import does.
 *
 * @param paths     The files, as the system named them.
 * @param scene     Receives a model.
 * @param resources Receives the assets.
 * @param state     Told what came of each.
 */
void importDropped(
    const std::vector<std::string>& paths,
    Scene& scene,
    ResourceManager& resources,
    EditorState& state
);

} // namespace FileImport

} // namespace Vkm::Engine
