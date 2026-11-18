#pragma once

// A small game project, made as its author would: mesh recipes in its library, and a scene of
// crates, each with a mesh and the fixture module's Counter, saved as the editor saves one.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "io/asset/asset_library.h"
#include "io/project/roots.h"
#include "io/scene/component_serializer.h"
#include "io/scene/scene_serializer.h"
#include "resource/asset/mesh_asset.h"
#include "system/script/script_component.h"

#include "harness/check.h"
#include "harness/world.h"

namespace Vkm::Test {

/// A generated mesh's recipe in the open project's library: "cube", or "sphere" of @p resolution.
inline void writeMeshRecipe(const std::string& name, const std::string& shape, int resolution = 0) {
    nlohmann::json source = {{"kind", "generator"}, {"type", shape}};
    if (resolution > 0) source["params"] = {{"resolution", resolution}};
    REQUIRE(AssetLibrary::writeRecipe(AssetType::Mesh, name, source));
}

/**
 * @brief An entity a scene starts with: a name, a place, a mesh by name, and a Counter at @p speed if not 0.
 */
struct Placed {
    std::string name;
    std::string mesh;
    glm::vec3   position{0.0f};
    float       speed = 0.0f;
};

/**
 * @brief Save a scene of @p placed at @p file in the open project.
 *
 * Made with no module loaded and no asset imported: a Counter is written by its name and fields, and
 * a mesh by its name, which only the library's recipe and a cook make a mesh of.
 */
inline void authorScene(const std::string& file, const std::vector<Placed>& placed) {
    Frame author;
    for (const Placed& each : placed) {
        const EntityId id = author.scene.createEntity();
        author.scene.add(id, makeName(each.name.c_str()));
        Transform at;
        at.position = each.position;
        author.scene.add(id, at);
        if (!each.mesh.empty()) {
            Mesh mesh;
            mesh.mesh = author.resources.add(MeshAsset{}, each.mesh);
            author.scene.add(id, mesh);
        }
        if (each.speed != 0.0f) {
            nlohmann::json script;
            script["behaviors"] = {
                {{"type", "Counter"}, {"properties", {{"speed", each.speed}, {"label", each.name}}}}
            };
            ScriptComponent counter;
            ComponentSerializer::load(script, counter);
            author.scene.add(id, std::move(counter));
        }
    }
    REQUIRE(SceneSerializer::save(author.scene, author.resources, (Roots::projectRoot() / file).string()));
}

/// The entity of @p scene named @p name, lowest slot first; null when none is.
inline EntityId entityNamed(const Scene& scene, const std::string& name) {
    return findLowestSlot<Name>(scene, [&](const Name& named) { return name == named.value; });
}

} // namespace Vkm::Test
