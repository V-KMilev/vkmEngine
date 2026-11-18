#include "gl_forward_programs.h"

#include "vkm_log/assert.h"

#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "storage/light/gl_ibl.h"

namespace Vkm::Engine {

GLForwardPrograms::Programs::Programs()
    : pbr("shaders/opengl/scene/forward/pbr")
    , plain("shaders/opengl/scene/forward/pbr", {"PLAIN"})
    , layered("shaders/opengl/scene/forward/pbr", {"PLAIN", "LAYERED"})
    , skinned("shaders/opengl/scene/forward/pbr", {"SKINNED"})
    , plainSkinned("shaders/opengl/scene/forward/pbr", {"SKINNED", "PLAIN"})
    , baked("shaders/opengl/scene/forward/pbr", {"BAKED"})
    , bakedPlain("shaders/opengl/scene/forward/pbr", {"PLAIN", "BAKED"})
    , bakedLayered("shaders/opengl/scene/forward/pbr", {"PLAIN", "LAYERED", "BAKED"})
    , water("shaders/opengl/scene/forward/water")
{}

GLForwardPrograms::GLForwardPrograms() = default;

GLForwardPrograms::~GLForwardPrograms() = default;

void GLForwardPrograms::build() {
    if (!m_programs) m_programs = std::make_unique<Programs>();
}

GLForwardPrograms::Kind GLForwardPrograms::kindOf(const GLMaterial* material, bool skinned) {
    if (!material) return Kind::Whole;
    // A layered material's layers are a static surface's.
    if (material->layered() && !skinned) return Kind::Layered;
    return material->plain() ? Kind::Plain : Kind::Whole;
}

Vkm::OpenGL::Shader& GLForwardPrograms::programFor(const GLMaterial* material, bool skinned, bool baked) {
    VKM_ASSERT(m_programs, "GLForwardPrograms::programFor before build()");
    Programs& p = *m_programs;
    switch (kindOf(material, skinned)) {
        case Kind::Plain:   return skinned ? p.plainSkinned : baked ? p.bakedPlain : p.plain;
        case Kind::Whole:   return skinned ? p.skinned : baked ? p.baked : p.pbr;
        case Kind::Layered: return baked ? p.bakedLayered : p.layered;
    }
    return p.pbr;
}

Vkm::OpenGL::Shader& GLForwardPrograms::water() {
    VKM_ASSERT(m_programs, "GLForwardPrograms::water before build()");
    return m_programs->water;
}

void GLForwardPrograms::zeroUniforms() {
    forEach(true, true, true, [](Vkm::OpenGL::Shader& program) { program.zeroUniforms(); });
}

void GLForwardPrograms::bindOffline(Vkm::OpenGL::Shader& program, const GLIBL& ibl) {
    const bool hasIBL = ibl.isReady();

    program.bind();
    program.setUniform1i("u_hasIBL", hasIBL ? 1 : 0);
    program.setUniform1f("u_iblIntensity", 1.0f);
    if (hasIBL) {
        ibl.bindIrradiance(GLBindings::IBLTextureSlots::IRRADIANCE);
        ibl.bindPrefilter(GLBindings::IBLTextureSlots::PREFILTER);
    }
    ibl.bindBrdf(GLBindings::IBLTextureSlots::BRDF_LUT);
    program.setUniform1i("u_hasAO", 0);
    program.setUniform1i("u_probeCount", 0);
    program.setUniform1i("u_useClusters", 0);
    program.setUniform1i("u_hasIrradianceVolume", 0);
    program.setUniform1i("u_hasFog", 0);
}

} // namespace Vkm::Engine
