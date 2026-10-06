#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "ecs/component/core/missing_assets.h"
#include "ecs/entity.h"
#include "ecs/entity_mapping.h"
#include "io/scene/prefab.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief Save / load a Scene + the assets it references to/from JSON.
 *
 * An `assets` block sits beside `entities`, rebuilt through
 * AssetSerializer::loadAssets; components name assets by `name`.
 *
 * load() stages the scene and the asset graph and swaps both in only on full
 * success; loadFromString() stages the scene only. Entities return to their
 * saved slots (Scene::createEntityAt), so references in the file need no remap.
 */
namespace SceneSerializer {
    /**
     * @brief Write every serialized component of @p id into @p out, naming the
     *        entities they refer to by their scene slot.
     *
     * @param scene     Scene holding the entity.
     * @param id        Entity to serialize.
     * @param out       JSON object to fill, keyed by component name.
     * @param resources Resolves asset handles to their names.
     */
    void saveComponents(
        const Scene& scene,
        EntityId id,
        nlohmann::json& out,
        const ResourceManager& resources
    );

    /**
     * @brief Write every serialized component of @p id into @p out, naming the
     *        entities they refer to through @p name.
     *
     * For a carrier with its own numbering, such as a prefab's file order.
     *
     * @param scene     Scene holding the entity.
     * @param id        Entity to serialize.
     * @param out       JSON object to fill, keyed by component name.
     * @param resources Resolves asset handles to their names.
     * @param name      Names the entities the components refer to.
     */
    void saveComponents(
        const Scene& scene,
        EntityId id,
        nlohmann::json& out,
        const ResourceManager& resources,
        const EntityNamer& name
    );

    /**
     * @brief Add every component present in @p src to @p entity.
     *
     * Hierarchy is not read: its parent may not exist yet, so callers wire it in
     * a second pass.
     *
     * @param src       JSON object written by saveComponents.
     * @param scene     Scene to add into.
     * @param entity    The entity receiving the components.
     * @param resources Resolves asset names back to handles.
     * @param resolve   Reads the carrier's names for entities back to entities.
     */
    void loadComponents(
        const nlohmann::json& src,
        Scene& scene,
        EntityId entity,
        const ResourceManager& resources,
        const EntityResolver& resolve
    );

    /**
     * @brief Save @p scene + the assets it references to @p path.
     *
     * @param scene     Scene to write.
     * @param resources Resolves each handle the scene holds to the name written.
     * @param path      File to write.
     * @return true on success; false (and a logged error) on I/O failure.
     */
    [[nodiscard]] bool save(const Scene& scene, const ResourceManager& resources, const std::string& path);

    /**
     * @brief Load a scene from @p path, replacing the live @p scene atomically.
     *
     * @param scene     Scene to replace.
     * @param resources Asset graph, replaced by the one the file's assets build.
     * @param path      File to read.
     * @return true on success; false (and a logged error) on failure, with
     *         the live scene untouched.
     */
    [[nodiscard]] bool load(Scene& scene, ResourceManager& resources, const std::string& path);

    /**
     * @brief The entries of @p id's unresolved-reference record whose field is
     *        still empty.
     *
     * The record is never trimmed, since an undo can empty a filled field again
     * and the name must still be there; readers ask this instead. Answered by
     * writing the entity and reading the field back, as the save does.
     *
     * @param scene Scene holding the entity.
     * @param resources Asset graph the entity's references resolve against.
     * @param id Entity to ask about.
     * @return The entries still unresolved, in record order; empty when none are.
     */
    std::vector<MissingAssetRef> unresolvedRefs(
        const Scene& scene,
        const ResourceManager& resources,
        EntityId id
    );

    /**
     * @brief Serialize @p scene + the assets it references to an in-memory JSON
     *        string (same content as save(), no file written).
     *
     * Pairs with loadFromString(); see PlaySnapshot.
     *
     * @param scene     Scene to write.
     * @param resources Resolves each handle the scene holds to the name written.
     * @return The serialized document.
     */
    std::string saveToString(const Scene& scene, const ResourceManager& resources);

    /**
     * @brief Load a scene from an in-memory JSON string produced by
     *        saveToString(), replacing @p scene and merging into @p resources.
     *
     * The scene swaps in as in load(); the asset graph is not swapped, so every
     * handle issued from it stays valid. The document stores an instance as a
     * reference, so @p instanceSlots restores where its own entities stood.
     *
     * @param text A document from saveToString().
     * @param scene Scene to replace on success.
     * @param resources Asset graph to resolve against, added to only where a
     *        name is absent.
     * @param instanceSlots Optional: Prefab::instanceSlotsOf the source world;
     *        without it an instance's entities take whatever slots are free.
     * @return true on success; false (and a logged error) on failure.
     */
    [[nodiscard]] bool loadFromString(
        const std::string& text,
        Scene& scene,
        ResourceManager& resources,
        const Prefab::InstanceSlots* instanceSlots = nullptr
    );

} // namespace SceneSerializer

} // namespace Vkm::Engine
