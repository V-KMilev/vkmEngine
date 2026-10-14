#include "offline/gl_capture_shadow.h"

#include <algorithm>
#include <limits>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "gl_buffer_upload.h"
#include "gl_context.h"
#include "gl_sampler.h"
#include "gl_texture.h"
#include "gl_uniform_buffer.h"

#include "convention/gl_bindings.h"
#include "convention/gl_depth.h"
#include "asset/gl_material.h"
#include "frame/gl_lights.h"
#include "frame/gl_object_buffer.h"
#include "gl_view.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

/// The sun's map over a capture region; the 3x3 kernel softens its texels.
constexpr uint32_t SHADOW_RES = 2048;

/// Kept clear of the region's edge on every side, in the map's own world units.
constexpr float SHADOW_MARGIN = 0.5f;

/// A sun's direction is told apart in steps of 1/this a component (keyFor), about a fifth of a
/// degree: a sun crossing the sky draws its map when it has moved that far, not every frame it moves.
constexpr float SHADOW_DIRECTION_STEPS = 256.0f;

} // namespace

GLCaptureShadow::GLCaptureShadow()
    : m_depth("shaders/shadow/depth")
    , m_masked("shaders/shadow/depth_masked") {}

GLCaptureShadow::~GLCaptureShadow() = default;

GLCaptureShadow::ShadowKey GLCaptureShadow::keyFor(const RenderView& view, const Math::AABB& region) {
    ShadowKey key;
    key.region = region;
    if (const LightData* light = lowestSlotDirectionalCaster(view.lights)) {
        key.casts      = true;
        key.light      = light->entitySlot;
        const glm::vec3 steps = glm::round(glm::normalize(light->direction) * SHADOW_DIRECTION_STEPS);
        key.direction  = steps / SHADOW_DIRECTION_STEPS;
        key.bias       = light->shadowBias;
        key.normalBias = light->shadowNormalBias;
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
    const size_t lightCount = std::min<size_t>(view.lights.size(), Config::MAX_LIGHTS);
    m_lightSlots.assign(lightCount, -1);
    const LightData* sun = lowestSlotDirectionalCaster(view.lights);
    if (sun && shadow.casts) {
        const auto index = static_cast<size_t>(sun - view.lights.data());
        if (index < lightCount) m_lightSlots[index] = 0;
    }
    lights.update(view.lights, {m_lightSlots.data(), static_cast<uint32_t>(lightCount)});
    if (!shadow.casts) return;

    shadow.ubo->bindBase(GLBindings::UBOBindingPoints::SHADOW);
    shadow.map->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
    m_sampler->bindSlot(GLBindings::ShadowTextureSlots::ATLAS_2D);
}

void GLCaptureShadow::draw(
    Vkm::GL::Context& gl,
    const RenderView& view,
    const GLView& glView,
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
    const LightData* key = lowestSlotDirectionalCaster(view.lights);
    shadow.key   = shadowFor;
    shadow.casts = key && !casters.empty();
    if (!shadow.casts) {
        use(slot, view, lights);
        return;
    }

    const RenderObjects& scene     = *view.objects;
    const glm::vec3      dir       = glm::normalize(key->direction);
    const glm::mat4      lightView = glm::lookAt(glm::vec3(0.0f), dir, stableUp(dir));

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
    const float     zNear   = -hi.z - SHADOW_MARGIN;
    const float     zFar    = -lo.z + SHADOW_MARGIN;
    const glm::mat4 lightVP = glm::ortho(lo.x, hi.x, lo.y, hi.y, zNear, zFar) * lightView;

    if (!shadow.map) {
        Vkm::GL::Texture2DParams params;
        params.width           = SHADOW_RES;
        params.height          = SHADOW_RES;
        params.internalFormat  = GL_DEPTH_COMPONENT24;
        params.format          = GL_DEPTH_COMPONENT;
        params.type            = GL_FLOAT;
        params.minFilter       = Vkm::GL::TextureMinFilter::Linear;
        params.magFilter       = Vkm::GL::TextureMagFilter::Linear;
        params.wrapS           = Vkm::GL::TextureWrap::ClampToEdge;
        params.wrapT           = Vkm::GL::TextureWrap::ClampToEdge;
        params.generateMipmaps = false;
        shadow.map = std::make_unique<Vkm::GL::Texture2D>("capture_shadow", params);
    }
    if (!m_sampler) {
        Vkm::GL::Sampler::Params compare;
        compare.minFilter = Vkm::GL::TextureMinFilter::Linear;
        compare.magFilter = Vkm::GL::TextureMagFilter::Linear;
        compare.wrapS     = Vkm::GL::TextureWrap::ClampToEdge;
        compare.wrapT     = Vkm::GL::TextureWrap::ClampToEdge;
        compare.compare   = Vkm::GL::TextureCompare::LessEqual;
        m_sampler = std::make_unique<Vkm::GL::Sampler>(compare);
    }

    // Casters through the static depth programs (bind pose, as the capture draws a character),
    // a cutout through the one its material cuts.
    m_fbo.bind();
    m_fbo.attachTexture(GL_DEPTH_ATTACHMENT, shadow.map->getID());
    m_fbo.setDrawBuffer(GL_NONE);
    m_fbo.setReadBuffer(GL_NONE);
    gl.setViewport(0, 0, static_cast<int32_t>(SHADOW_RES), static_cast<int32_t>(SHADOW_RES));
    gl.setDepthTest(true);
    gl.setDepthWrite(true);
    gl.setDepthFunc(GLDepth::SHADOW_TEST);
    gl.setBlending(false);
    gl.setFaceCulling(false);
    gl.clear(false, true, false, GLDepth::SHADOW_CLEAR);

    m_batcher.buildGrouped(m_overRegion, scene, glView, 0);
    m_masked.bind();
    m_masked.setUniformMatrix4fv("u_lightVP", GLDepth::shadowProjection(lightVP));
    m_masked.setUniform2f("u_ditherShift", 0.0f, 0.0f);
    m_depth.bind();
    m_depth.setUniformMatrix4fv("u_lightVP", GLDepth::shadowProjection(lightVP));
    objects.bind();
    const Vkm::GL::Shader* bound = &m_depth;
    for (const InstanceDraw& draw : m_batcher.draws()) {
        const GLMaterial* material = glView.getMaterial(draw.material);
        const bool        masked   = material && material->cutsItsShadow();
        Vkm::GL::Shader&  program  = masked ? m_masked : m_depth;
        if (&program != bound) {
            program.bind();
            bound = &program;
        }
        if (masked) {
            material->bind(GLBindings::UBOBindingPoints::MATERIAL);
            material->bindTextures(glView);
        }
        m_batcher.draw(gl, draw);
    }

    // One "cascade" covering everything: the whole map, and a split no view
    // depth reaches.
    ShadowUBOData data{};
    data.csmBase       = 0;
    data.csmCount      = 1;
    data.cascadeSplits = glm::vec4(std::numeric_limits<float>::max());
    Shadow2DGPU& map = data.s2d[0];
    map.lightVP = lightVP;
    map.atlas   = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const float worldTexel = std::max(hi.x - lo.x, hi.y - lo.y) / static_cast<float>(SHADOW_RES);
    map.params  = glm::vec4(key->shadowBias, worldTexel, zFar - zNear, 0.0f);  // w = 0: the hard kernel
    map.shape   = glm::vec4(key->shadowNormalBias, 0.0f, 0.0f, 0.0f);
    Vkm::GL::uploadIfChanged(shadow.ubo, shadow.last, data);
    use(slot, view, lights);
}

} // namespace Vkm::Engine
