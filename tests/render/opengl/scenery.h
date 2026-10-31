#pragma once

// What the GPU tests' scenes are built of, placed as a scene author places them: shapes, materials,
// an eye, a sun and lamps.

#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/light/irradiance_volume.h"
#include "ecs/component/light/light.h"
#include "ecs/component/light/reflection_probe.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "io/asset/generate/mesh_generators.h"
#include "mesh/lightmap_unwrap.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/texture_asset.h"
#include "resource/asset/texture_params.h"
#include "resource/resource_manager.h"

#include "render/opengl/gpu.h"

namespace Vkm::Test {

using namespace Vkm::Engine;

/**
 * @brief The engine's own unit shapes, added once to a test's resources.
 */
struct Shapes {
    MeshHandle cube;
    MeshHandle sphere;

    explicit Shapes(ResourceManager& resources)
        : cube(resources.add(generateCube(), "test:cube"))
        , sphere(resources.add(generateSphere(), "test:sphere"))
    {}
};

/// A plain material of one color.
inline MaterialHandle material(
    ResourceManager& resources,
    const std::string& name,
    const glm::vec3& albedo,
    float roughness = 0.6f,
    float metallic = 0.0f
) {
    MaterialAsset made;
    made.albedo    = glm::vec4(albedo, 1.0f);
    made.roughness = roughness;
    made.metallic  = metallic;
    return resources.add(std::move(made), name);
}

/**
 * @brief A square texture made in code, repeating.
 *
 * @param resources Where it goes.
 * @param name      Its name.
 * @param size      Texels a side.
 * @param usage     Color (sRGB), a normal map (its X and Y) or data.
 * @param of        Each texel's value from its x and y, top row first: RGBA, or a normal's X and Y in
 *                  0..1.
 * @return Its handle.
 */
template<typename Of>
TextureHandle texture(
    ResourceManager& resources,
    const std::string& name,
    uint32_t size,
    TextureUsage usage,
    Of of
) {
    TextureAsset made;
    const uint32_t channels = usage == TextureUsage::Normal ? 2u : 4u;
    made.params.width          = size;
    made.params.height         = size;
    made.params.internalFormat = inferInternalFormat(static_cast<int>(channels), usage);
    made.params.format         = usage == TextureUsage::Normal ? TexturePixelFormat::RG : TexturePixelFormat::RGBA;
    made.params.wrapS          = TextureWrapMode::Repeat;
    made.params.wrapT          = TextureWrapMode::Repeat;
    made.pixelData.resize(static_cast<size_t>(size) * size * channels);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const glm::vec4 value = glm::clamp(glm::vec4(of(x, y)), 0.0f, 1.0f);
            for (uint32_t c = 0; c < channels; ++c) {
                made.pixelData[(static_cast<size_t>(y) * size + x) * channels + c] =
                    static_cast<uint8_t>(value[static_cast<int>(c)] * 255.0f + 0.5f);
            }
        }
    }
    return resources.add(std::move(made), name);
}

/// @p mesh in @p look at @p at, scaled by @p scale.
inline EntityId place(
    Scene& scene,
    MeshHandle mesh,
    MaterialHandle look,
    const glm::vec3& at,
    const glm::vec3& scale = glm::vec3(1.0f),
    const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    pose.scale    = scale;
    pose.rotation = rotation;
    scene.add(id, pose);
    Mesh drawn;
    drawn.mesh     = mesh;
    drawn.material = look;
    scene.add(id, drawn);
    return id;
}

/// A slab 40 m across whose top is at 0.
inline EntityId addFloor(Scene& scene, const Shapes& shapes, MaterialHandle look) {
    return place(scene, shapes.cube, look, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 40.0f});
}

/// The scene's camera at @p eye, looking at @p target, @p up its frame's top.
inline EntityId addEye(
    Scene& scene,
    const glm::vec3& eye,
    const glm::vec3& target,
    const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)
) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = eye;
    pose.rotation = glm::quatLookAt(glm::normalize(target - eye), up);
    scene.add(id, pose);
    scene.add(id, Camera{});
    return id;
}

/// The key light, where the scene's sky puts the sun; the sky aims and colors it each frame.
inline EntityId addSun(Scene& scene, float intensity = 3.0f) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.rotation = glm::quatLookAt(-scene.environment().sunDirection(), glm::vec3(0.0f, 1.0f, 0.0f));
    scene.add(id, pose);
    Light light;
    light.type        = LightType::Directional;
    light.intensity   = intensity;
    light.castShadows = true;
    scene.add(id, light);
    return id;
}

/// A point lamp at @p at, casting no shadow.
inline EntityId addLamp(
    Scene& scene,
    const glm::vec3& at,
    const glm::vec3& color,
    float intensity,
    float radius
) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(id, pose);
    Light light;
    light.type        = LightType::Point;
    light.color       = color;
    light.intensity   = intensity;
    light.radius      = radius;
    light.castShadows = false;
    scene.add(id, light);
    return id;
}

/// Where @p world lands in the frame @p eye draws, in pixels from the top left.
inline glm::vec2 pixelOf(Scene& scene, EntityId eye, const glm::vec3& world) {
    const Transform& pose = scene.get<Transform>(eye);
    const glm::mat4 toEye = glm::inverse(
        glm::translate(glm::mat4(1.0f), pose.position) * glm::mat4_cast(pose.rotation)
    );
    const glm::mat4 lens  = Camera::computeProjection(scene.get<Camera>(eye), float(WIDTH) / float(HEIGHT));
    const glm::vec4 clip  = lens * toEye * glm::vec4(world, 1.0f);
    const glm::vec2 ndc   = glm::vec2(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * float(WIDTH), (0.5f - ndc.y * 0.5f) * float(HEIGHT)};
}

/**
 * @brief Where yard() puts what it shows, for a test to look at.
 */
struct Yard {
    EntityId red;     ///< A red box, left of the middle.
    EntityId blue;    ///< A blue box, right of the middle.
    EntityId sphere;  ///< A white sphere between them.
    EntityId eye;
};

/// A gray floor under the sun and the default sky, a red and a blue box and a white sphere on it,
/// seen from a few meters off: what most frame-wide checks draw. A floor of little @p roughness
/// mirrors them, and a dark one shows it best.
inline Yard yard(
    Scene& scene,
    ResourceManager& resources,
    float roughness = 0.8f,
    const glm::vec3& floor = glm::vec3(0.5f)
) {
    const Shapes shapes(resources);
    addFloor(scene, shapes, material(resources, "test:floor", floor, roughness));
    Yard yard;
    yard.red    = place(
        scene,
        shapes.cube,
        material(resources, "test:red", {0.8f, 0.05f, 0.05f}),
        {-1.6f, 0.5f, 0.0f}
    );
    yard.blue   = place(
        scene,
        shapes.cube,
        material(resources, "test:blue", {0.05f, 0.1f, 0.8f}),
        {1.6f, 0.5f, 0.0f}
    );
    yard.sphere = place(
        scene,
        shapes.sphere,
        material(resources, "test:white", {0.9f, 0.9f, 0.9f}, 0.3f),
        {0.0f, 0.5f, 0.0f}
    );
    addSun(scene);
    yard.eye = addEye(scene, {0.0f, 1.6f, 5.0f}, {0.0f, 0.5f, 0.0f});
    return yard;
}

/**
 * @brief What bakedRoom() puts where a test may move it.
 */
struct Room {
    EntityId volume;  ///< The irradiance volume filling it.
    EntityId probe;   ///< The reflection probe filling it.
    EntityId lamp;    ///< A fixed lamp in the far corner, its shadows baked.
    EntityId eye;
};

/**
 * @brief A room of fixed meshes under a fixed sun, 8 by 4 by 8 m inside, a doorway toward the eye
 *        and a window to the east: its light baked into lightmaps, an irradiance volume and a
 *        reflection probe that fill it, a chrome ball in it, and a lamp the captures leave out.
 */
inline Room bakedRoom(Scene& scene, ResourceManager& r) {
    const Shapes shapes(r);
    // A cube unwrapped as the cook unwraps a mesh, so its fixed light is baked into a lightmap.
    MeshAsset unwrapped = generateCube();
    unwrapLightmap(unwrapped, "test:block");
    const MeshHandle block = r.add(std::move(unwrapped), "test:block");
    const MaterialHandle wall      = material(r, "test:wall", {0.8f, 0.78f, 0.72f}, 0.9f);
    const MaterialHandle floorLook = material(r, "test:boards", {0.6f, 0.4f, 0.25f}, 0.7f);
    const auto fixed = [&](MeshHandle mesh, MaterialHandle look, glm::vec3 at, glm::vec3 scale) {
        scene.get<Mesh>(place(scene, mesh, look, at, scale)).movable = false;
    };
    fixed(block, floorLook, {0.0f, -0.2f, 0.0f}, {12.0f, 0.4f, 12.0f});
    fixed(block, wall, {0.0f, 4.2f, 0.0f}, {8.8f, 0.4f, 8.8f});
    fixed(block, wall, {0.0f, 2.0f, -4.2f}, {8.8f, 4.0f, 0.4f});
    fixed(block, wall, {-4.2f, 2.0f, 0.0f}, {0.4f, 4.0f, 8.8f});
    fixed(block, wall, {4.2f, 2.0f, -2.5f}, {0.4f, 4.0f, 3.8f});
    fixed(block, wall, {4.2f, 2.0f, 3.0f}, {0.4f, 4.0f, 2.8f});
    fixed(block, wall, {4.2f, 3.4f, 0.25f}, {0.4f, 1.2f, 2.7f});
    fixed(block, wall, {-2.6f, 2.0f, 4.2f}, {3.6f, 4.0f, 0.4f});
    fixed(block, wall, {2.6f, 2.0f, 4.2f}, {3.6f, 4.0f, 0.4f});
    fixed(block, material(
        r,
        "test:table",
        {0.3f, 0.5f, 0.3f},
        0.6f
    ), {-1.5f, 0.5f, -1.5f}, {1.5f, 1.0f, 1.0f});

    // In through the window.
    scene.environment().sky.sunAzimuth   = 100.0f;
    scene.environment().sky.sunElevation = 35.0f;
    scene.get<Light>(addSun(scene)).movable = false;

    Room room;
    Transform filling;
    filling.position = {0.0f, 2.0f, 0.0f};
    room.volume      = scene.createEntity();
    scene.add(room.volume, filling);
    IrradianceVolume grid;
    grid.halfExtents = {4.0f, 2.0f, 4.0f};
    scene.add(room.volume, grid);
    room.probe = scene.createEntity();
    scene.add(room.probe, filling);
    ReflectionProbe captured;
    captured.halfExtents = {4.0f, 2.0f, 4.0f};
    captured.resolution  = 64;
    scene.add(room.probe, captured);
    place(
        scene,
        shapes.sphere,
        material(r, "test:chrome", glm::vec3(0.95f), 0.05f, 1.0f),
        {1.0f, 0.6f, -1.0f},
        glm::vec3(1.2f)
    );

    room.lamp = addLamp(scene, {-3.2f, 3.0f, -3.2f}, {0.6f, 0.8f, 1.0f}, 10.0f, 5.0f);
    scene.get<Light>(room.lamp).castShadows = true;
    scene.get<Light>(room.lamp).movable     = false;
    // Its light reaches the wall it faces; no capture holds it.
    scene.get<Light>(addLamp(scene, {-3.6f, 2.5f, 1.5f}, {1.0f, 0.5f, 0.2f}, 15.0f, 3.0f)).inCaptures = false;

    room.eye = addEye(scene, {2.8f, 1.7f, 3.2f}, {-1.5f, 1.0f, -1.5f});
    return room;
}

} // namespace Vkm::Test
