#include "offline/capture/gl_capture_shadow.h"

#include <algorithm>
#include <limits>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "vkm_opengl/buffer/buffer_upload.h"
#include "vkm_opengl/buffer/uniform_buffer.h"
#include "vkm_opengl/core/context.h"
#include "vkm_opengl/texture/sampler.h"
#include "vkm_opengl/texture/texture.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "frame/light/gl_lights.h"
#include "frame/gl_object_buffer.h"
#include "asset/gl_asset_mirror.h"
#include "base/math/axes.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t SHADOW_RES = 2048;  ///< The sun's map over a capture region; the 3x3 kernel softens its
                                       ///< texels.

constexpr float SHADOW_MARGIN = 0.5f;  ///< Kept clear of the region's edge on every side, in the map's own
                                       ///< world units.

constexpr float SHADOW_DIRECTION_STEPS = 256.0f;  ///< A sun's direction is told apart in steps of 1/this a
                                                  ///< component (keyFor), about a fifth of a degree: a sun
                                                  ///< crossing the sky draws its map when it has moved that
                                                  ///< far, not every frame it moves.

} // namespace

GLCaptureShadow::GLCaptureShadow() = default;

GLCaptureShadow::~GLCaptureShadow() = default;

GLCaptureShadow::ShadowKey GLCaptureShadow::keyFor(const RenderView& view, const Math::AABB& region) {
    ShadowKey key;
    key.region = region;
    key.world  = view.worldEpoch;
    if (const LightData* light = lowestSlotDirectionalCaster(view.lists.lights)) {
        const glm::vec3 steps = glm::round(glm::normalize(light->direction) * SHADOW_DIRECTION_STEPS);

        key.casts     = true;
        key.light     = light->entitySlot;
        key.direction = steps / SHADOW_DIRECTION_STEPS;
    }
    return key;
}

bool GLCaptureShadow::useKept(const RenderView& view, const Math::AABB& region, GLLights& lights) {
    const ShadowKey key = keyFor(view, region);
    for (int slot = 0; slot < SHADOW_SLOTS; ++slot) {
        if (m_slots[slot].key && *m_slots[slot].key == key) {
            use(slot, view, lights);
            return true;
        }
    }
    return false;
}

void GLCaptureShadow::use(int slot, const RenderView& view, GLLights& lights) {
    ShadowSlot& shadow = m_slots[slot];
    shadow.used = ++m_clock;

    // The lights, the shadowed sun on the map's slot while it casts.
    const size_t lightCount = std::min<size_t>(view.lists.lights.size(), Config::MAX_LIGHTS);
    m_lightSlots.assign(lightCount, -1);
    const LightData* sun = lowestSlotDirectionalCaster(view.lists.lights);
    if (sun && shadow.casts) {
        const auto index = static_cast<size_t>(sun - view.lists.lights.data());
        if (index < lightCount) m_lightSlots[index] = 0;
    }
    const bool steady = true;
    lights.update(view.lists.lights, {m_lightSlots.data(), static_cast<uint32_t>(lightCount)}, steady);
    if (!shadow.casts) return;

    shadow.ubo->bindBase(GLBindings::UBOBindingPoints::SHADOW);
    shadow.map->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
    m_sampler->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
}

void GLCaptureShadow::draw(
    Vkm::OpenGL::Context& gl,
    const RenderView& view,
    const GLAssetMirror& mirror,
    const GLObjectBuffer& objects,
    const std::vector<uint32_t>& casters,
    const Math::AABB& region,
    GLLights& lights
) {
    // Into the slot drawn for this before, or else the one bound longest ago.
    const ShadowKey shadowFor = keyFor(view, region);
    int slot = 0;
    for (int i = 0; i < SHADOW_SLOTS; ++i) {
        if (m_slots[i].key && *m_slots[i].key == shadowFor) {
            slot = i;
            break;
        }
        if (m_slots[i].used < m_slots[slot].used) slot = i;
    }
    ShadowSlot& shadow = m_slots[slot];

    // The sun the frame shadows (see lowestSlotDirectionalCaster).
    const LightData* key = lowestSlotDirectionalCaster(view.lists.lights);
    shadow.key   = shadowFor;
    shadow.casts = key && !casters.empty();
    if (!shadow.casts) {
        use(slot, view, lights);
        return;
    }

    const RenderObjects& scene = *view.objects;
    const glm::vec3 dir = glm::normalize(key->direction);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), dir, Math::stableUp(dir));

    const Math::AABB across = Math::transform(lightView, region);
    glm::vec3 lo = across.min - glm::vec3(SHADOW_MARGIN);
    glm::vec3 hi = across.max + glm::vec3(SHADOW_MARGIN);

    // Only those over the region's footprint draw: the map shows nothing else. Its depth reaches
    // each, since a roof well above a volume still shades it.
    m_overRegion.clear();
    for (const uint32_t object : casters) {
        const Math::AABB c = Math::transform(lightView, scene.bounds[object]);
        if (c.max.x < lo.x || c.min.x > hi.x || c.max.y < lo.y || c.min.y > hi.y) continue;
        lo.z = std::min(lo.z, c.min.z);
        hi.z = std::max(hi.z, c.max.z);
        m_overRegion.push_back(object);
    }

    // The view looks down -z, so what lies nearest the light has the largest z.
    const float zNear = -hi.z - SHADOW_MARGIN;
    const float zFar  = -lo.z + SHADOW_MARGIN;
    const glm::mat4 lightVP = glm::ortho(lo.x, hi.x, lo.y, hi.y, zNear, zFar) * lightView;

    if (!shadow.map) {
        Vkm::OpenGL::Texture2DParams params;
        params.width           = SHADOW_RES;
        params.height          = SHADOW_RES;
        params.internalFormat  = GL_DEPTH_COMPONENT24;
        params.format          = GL_DEPTH_COMPONENT;
        params.type            = GL_FLOAT;
        params.minFilter       = Vkm::OpenGL::TextureMinFilter::Linear;
        params.magFilter       = Vkm::OpenGL::TextureMagFilter::Linear;
        params.wrapS           = Vkm::OpenGL::TextureWrap::ClampToEdge;
        params.wrapT           = Vkm::OpenGL::TextureWrap::ClampToEdge;
        params.generateMipmaps = false;
        shadow.map             = std::make_unique<Vkm::OpenGL::Texture2D>("capture_shadow", params);
    }
    if (!m_sampler) {
        Vkm::OpenGL::Sampler::Params compare;
        compare.minFilter = Vkm::OpenGL::TextureMinFilter::Linear;
        compare.magFilter = Vkm::OpenGL::TextureMagFilter::Linear;
        compare.wrapS     = Vkm::OpenGL::TextureWrap::ClampToEdge;
        compare.wrapT     = Vkm::OpenGL::TextureWrap::ClampToEdge;
        compare.compare   = GLDepth::SHADOW_COMPARE;
        m_sampler         = std::make_unique<Vkm::OpenGL::Sampler>(compare);
    }

    // Casters in their bind pose, as the capture draws a character.
    m_fbo.bind();
    m_fbo.attachTexture(GL_DEPTH_ATTACHMENT, shadow.map->getID());
    m_fbo.setDrawBuffer(GL_NONE);
    m_fbo.setReadBuffer(GL_NONE);
    m_casters.draw(gl, m_overRegion, scene, mirror, objects, GLDepth::shadowProjection(lightVP), SHADOW_RES);

    // One "cascade" covering everything: the whole map, read as the tile of what moves, with a reach
    // no distance passes and no held layer.
    ShadowUBOData data{};
    data.csmBase       = 0;
    data.csmCount      = 1;
    data.moverCount    = 1;
    data.cascadeSplits = glm::vec4(std::numeric_limits<float>::max());

    const float worldTexel = std::max(hi.x - lo.x, hi.y - lo.y) / static_cast<float>(SHADOW_RES);
    const float width      = worldTexel * static_cast<float>(SHADOW_RES);
    data.s2d[0] = Shadow2DGPU::orthographic(lightVP, worldTexel, width, zFar - zNear, 0.0f);  // hard kernel
    Vkm::OpenGL::uploadIfChanged(shadow.ubo, shadow.last, data);
    use(slot, view, lights);
}

} // namespace Vkm::Engine
