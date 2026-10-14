#define VKM_LOG_CATEGORY "CITY"

#include "city_block.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "logger.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/mesh.h"
#include "resource/generate/mesh_generators.h"
#include "resource/resource_manager.h"

namespace City {

namespace {

constexpr uint64_t CITY_SEED = 0xC17Bu;

constexpr float FLOOR_HEIGHT  = 3.5f;
constexpr float BUILDING_DEEP = 12.0f;  ///< From a building's street face back into its block.
/// Metres ahead along the loop the camera looks at, so it turns through a corner, not at it.
constexpr float LOOK_AHEAD    = 10.0f;

/// Each side of a block, as the way it faces out.
const glm::vec3 BLOCK_SIDES[4] = {
    {0.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, -1.0f},
    {-1.0f, 0.0f, 0.0f},
};

MaterialHandle makeMaterial(
    ResourceManager& resources,
    const char* name,
    const glm::vec3& albedo,
    float roughness,
    float metallic
) {
    MaterialAsset material;
    material.albedo    = glm::vec4(albedo, 1.0f);
    material.roughness = roughness;
    material.metallic  = metallic;
    return resources.add(std::move(material), name);
}

} // namespace

void CityBlock::onStart() {
    if (m_built) return;
    m_built = true;
    m_rng.seed(CITY_SEED);

    Environment& environment = scene().environment();
    environment.sky.showSkybox     = true;
    environment.sky.procedural     = true;
    environment.sky.sunElevation   = 38.0f;
    environment.sky.sunAzimuth     = 35.0f;
    environment.sky.lightIntensity = 3.0f;

    m_camera = findActiveCamera(scene());
    if (Camera* camera = scene().tryGet<Camera>(m_camera)) {
        camera->zFar  = CAMERA_FAR;
        camera->zNear = 0.1f;
    }

    m_cube     = resources().add(generateCube(), "city:cube");
    m_sphere   = resources().add(generateSphere(24, 12), "city:sphere");
    m_cylinder = resources().add(generateCylinder(0.5f, 1.0f, 20), "city:cylinder");

    ResourceManager& res = resources();
    m_ground   = makeMaterial(res, "city:asphalt", {0.18f, 0.18f, 0.19f}, 0.9f, 0.0f);
    m_walls[0] = makeMaterial(res, "city:concrete", {0.55f, 0.54f, 0.51f}, 0.85f, 0.0f);
    m_walls[1] = makeMaterial(res, "city:brick", {0.45f, 0.24f, 0.18f}, 0.9f, 0.0f);
    m_walls[2] = makeMaterial(res, "city:render", {0.72f, 0.68f, 0.60f}, 0.8f, 0.0f);
    m_metal    = makeMaterial(res, "city:metal", {0.62f, 0.63f, 0.65f}, 0.45f, 1.0f);
    m_ledge    = makeMaterial(res, "city:ledge", {0.66f, 0.65f, 0.62f}, 0.7f, 0.0f);
    m_cars[0]  = makeMaterial(res, "city:car_red", {0.55f, 0.05f, 0.05f}, 0.3f, 0.0f);
    m_cars[1]  = makeMaterial(res, "city:car_blue", {0.06f, 0.15f, 0.5f}, 0.3f, 0.0f);
    m_cars[2]  = makeMaterial(res, "city:car_white", {0.8f, 0.8f, 0.78f}, 0.3f, 0.0f);
    m_cars[3]  = makeMaterial(res, "city:car_black", {0.03f, 0.03f, 0.03f}, 0.25f, 0.0f);

    MaterialAsset glow;
    glow.albedo           = {1.0f, 0.9f, 0.7f, 1.0f};
    glow.emission         = {1.0f, 0.85f, 0.6f};
    glow.emissiveStrength = 4.0f;
    m_lampGlow = res.add(std::move(glow), "city:lamp_glow");

    // The streets run between the blocks and round the grid's edge.
    const float pitch  = blockSize + streetWidth;
    const float extent = static_cast<float>(blocks) * pitch + streetWidth;
    place(m_cube, m_ground, {0.0f, -0.5f, 0.0f}, {extent, 1.0f, extent});

    const float first = -0.5f * static_cast<float>(blocks - 1) * pitch;
    // The street a quarter of the grid out from the centre; with an odd count, the one beside it.
    m_loopRadius = std::max(1.0f, std::floor(static_cast<float>(blocks) * 0.25f)) * pitch
        - static_cast<float>(blocks % 2) * 0.5f * pitch;
    for (int z = 0; z < blocks; ++z) {
        for (int x = 0; x < blocks; ++x) {
            buildBlock({first + static_cast<float>(x) * pitch, 0.0f, first + static_cast<float>(z) * pitch});
        }
    }

    size_t meshes = 0;
    scene().forEach<Mesh>([&meshes](EntityId, Mesh&) { ++meshes; });
    LOG_INFO("built: %d x %d blocks, %zu meshes", blocks, blocks, meshes);
}

void CityBlock::place(
    MeshHandle mesh,
    MaterialHandle material,
    const glm::vec3& position,
    const glm::vec3& scale,
    const glm::quat& rotation
) {
    const EntityId entity = spawn();
    // The city never moves: the renderer keeps what it works out about each prop.
    Mesh drawn{mesh, material};
    drawn.movable = false;
    scene().add(entity, std::move(drawn));
    Transform transform;
    transform.position = position;
    transform.rotation = rotation;
    transform.scale    = scale;
    scene().add(entity, std::move(transform));
}

void CityBlock::buildBlock(const glm::vec3& centre) {
    const float half = blockSize * 0.5f;

    // Three buildings a side, each facing its street; the corners are the side's first.
    for (const glm::vec3& outward : BLOCK_SIDES) {
        const glm::vec3 along(-outward.z, 0.0f, outward.x);
        const float     span = blockSize / 3.0f;
        for (int b = 0; b < 3; ++b) {
            const float     offset = -half + span * (static_cast<float>(b) + 0.5f);
            const glm::vec3 at     = centre + outward * (half - BUILDING_DEEP * 0.5f) + along * offset;
            // Deep along its outward axis, a metre short of its span along the street.
            const glm::vec2 size = BUILDING_DEEP * glm::abs(glm::vec2(outward.x, outward.z))
                + (span - 1.0f) * glm::abs(glm::vec2(along.x, along.z));
            buildBuilding(at, size, outward);
        }

        // The pavement: lamps, then cars at the kerb.
        const glm::vec3 kerb = centre + outward * (half + 2.0f);
        for (int l = 0; l < lampsPerSide; ++l) {
            const float     t  = (static_cast<float>(l) + 0.5f) / static_cast<float>(lampsPerSide) - 0.5f;
            const glm::vec3 at = kerb + along * (t * blockSize);
            place(m_cylinder, m_metal, at + glm::vec3(0.0f, 2.5f, 0.0f), {0.15f, 5.0f, 0.15f});
            place(m_sphere, m_lampGlow, at + glm::vec3(0.0f, 5.1f, 0.0f), glm::vec3(0.5f));
        }
        const glm::vec3 lane = centre + outward * (half + 4.0f);
        for (int c = 0; c < carsPerSide; ++c) {
            const float     t      = (static_cast<float>(c) + 0.5f) / static_cast<float>(carsPerSide) - 0.5f;
            const glm::vec3 at     = lane + along * (t * blockSize + m_rng.nextFloat(-2.0f, 2.0f));
            const glm::vec3 length = along * 4.2f + outward * 1.8f;
            const glm::vec3 body(std::abs(length.x), 0.8f, std::abs(length.z));
            const MaterialHandle paint = m_cars[m_rng.nextInt(0, 3)];
            place(m_cube, paint, at + glm::vec3(0.0f, 0.7f, 0.0f), body);
            place(m_cube, paint, at + glm::vec3(0.0f, 1.35f, 0.0f), body * glm::vec3(0.6f, 0.7f, 0.6f));
            // Wheels: the cylinder's axis turned from up onto the axle, across the car.
            const glm::quat axle = glm::angleAxis(glm::half_pi<float>(), along);
            for (int w = 0; w < 4; ++w) {
                const float     ahead  = (w & 1) ? 1.4f : -1.4f;
                const float     aside  = (w & 2) ? 0.8f : -0.8f;
                const glm::vec3 corner = along * ahead + outward * aside + glm::vec3(0.0f, 0.35f, 0.0f);
                place(m_cylinder, m_cars[3], at + corner, {0.7f, 0.25f, 0.7f}, axle);
            }
        }
    }
}

void CityBlock::buildBuilding(const glm::vec3& centre, const glm::vec2& size, const glm::vec3& outward) {
    const int   floors = m_rng.nextInt(4, 20);
    const float height = static_cast<float>(floors) * FLOOR_HEIGHT;
    place(
        m_cube,
        m_walls[m_rng.nextInt(0, 2)],
        centre + glm::vec3(0.0f, height * 0.5f, 0.0f),
        {size.x, height, size.y}
    );

    // The street face's details, a row a floor.
    const glm::vec3 along(-outward.z, 0.0f, outward.x);
    const float     width = std::abs(outward.x) > 0.5f ? size.y : size.x;
    const glm::vec3 face  = centre + outward * (BUILDING_DEEP * 0.5f);
    for (int f = 1; f < floors; ++f) {
        const float y = static_cast<float>(f) * FLOOR_HEIGHT;
        for (int d = 0; d < detailsPerFloor; ++d) {
            const float     t  = (static_cast<float>(d) + 0.5f) / static_cast<float>(detailsPerFloor) - 0.5f;
            const glm::vec3 at = face + along * (t * width * 0.9f) + glm::vec3(0.0f, y, 0.0f);
            const int       kind = m_rng.nextInt(0, 9);
            if (kind < 6) {
                // A window ledge.
                const glm::vec3 ledge = along * 1.4f + outward * 0.35f;
                place(
                    m_cube,
                    m_ledge,
                    at + outward * 0.17f,
                    {std::abs(ledge.x) + 0.1f, 0.15f, std::abs(ledge.z) + 0.1f}
                );
            } else if (kind < 9) {
                // An air conditioner, hung below the floor's line.
                place(
                    m_cylinder,
                    m_metal,
                    at + outward * 0.45f - glm::vec3(0.0f, 0.6f, 0.0f),
                    glm::vec3(0.8f, 0.6f, 0.8f)
                );
            } else {
                // A dish, flat against the facade.
                place(
                    m_sphere,
                    m_metal,
                    at + outward * 0.5f + glm::vec3(0.0f, 0.8f, 0.0f),
                    glm::vec3(0.9f) - glm::abs(outward) * 0.6f
                );
            }
        }
    }
}

glm::vec3 CityBlock::loopPoint(float distance) const {
    // The street ringing the grid's middle, round and round.
    const float radius     = m_loopRadius;
    const float sideLength = radius * 2.0f;
    const float lap        = sideLength * 4.0f;
    const float d          = std::fmod(std::fmod(distance, lap) + lap, lap);
    const int   side       = static_cast<int>(d / sideLength);
    const float along      = d - static_cast<float>(side) * sideLength;

    const glm::vec3 corners[4] = {
        {-radius, 0.0f, -radius},
        {radius, 0.0f, -radius},
        {radius, 0.0f, radius},
        {-radius, 0.0f, radius},
    };
    const glm::vec3 from = corners[side];
    const glm::vec3 to   = corners[(side + 1) % 4];
    return from + glm::normalize(to - from) * along;
}

void CityBlock::onUpdate(float dt) {
    if (!scriptedCamera) return;
    Transform* view = scene().tryGet<Transform>(m_camera);
    if (!view) return;

    m_travelled += dt * cameraSpeed;

    // It looks at the loop ahead of it, so it turns through a corner as a driver would.
    const glm::vec3 position = loopPoint(m_travelled);
    const glm::vec3 heading  = glm::normalize(loopPoint(m_travelled + LOOK_AHEAD) - position);

    view->position = position + glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
    view->rotation = Math::lookRotation(heading);
}

} // namespace City
