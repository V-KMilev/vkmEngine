#include "gl_forward_programs.h"

#include "l_assert.h"

#include "asset/gl_material.h"
#include "convention/gl_bindings.h"
#include "storage/gl_ibl.h"

namespace Vkm::Engine {

GLForwardPrograms::Programs::Programs()
    : pbr("shaders/forward/pbr")
    , plain("shaders/forward/pbr_plain")
    , layered("shaders/forward/pbr_layered")
    , skinned("shaders/forward/pbr_skinned")
    , plainSkinned("shaders/forward/pbr_plain_skinned")
    , baked("shaders/forward/pbr_baked")
    , bakedPlain("shaders/forward/pbr_plain_baked")
    , bakedLayered("shaders/forward/pbr_layered_baked")
{}

GLForwardPrograms::GLForwardPrograms() = default;

GLForwardPrograms::~GLForwardPrograms() = default;

void GLForwardPrograms::build() {
    if (!m_programs) m_programs = std::make_unique<Programs>();
}

Vkm::GL::Shader& GLForwardPrograms::programFor(const GLMaterial* material, bool skinned, bool baked) {
    VKM_ASSERT(m_programs, "GLForwardPrograms::programFor before build()");
    Programs&  p       = *m_programs;
    const bool plain   = material && material->plain();
    const bool layered = material && material->layered();
    if (skinned) return plain ? p.plainSkinned : p.skinned;
    if (baked) return layered ? p.bakedLayered : plain ? p.bakedPlain : p.baked;
    return layered ? p.layered : plain ? p.plain : p.pbr;
}

void GLForwardPrograms::zeroUniforms() {
    forEach(true, true, [](Vkm::GL::Shader& program) { program.zeroUniforms(); });
}

void GLForwardPrograms::bindOffline(Vkm::GL::Shader& program, const GLIBL& ibl) {
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
    program.setUniform1i("u_hasScreenShadow", 0);
    program.setUniform1i("u_probeCount", 0);
    program.setUniform1i("u_useClusters", 0);
    program.setUniform1i("u_hardShadows", 1);
    program.setUniform1i("u_hasIrradianceVolume", 0);
    program.setUniform1i("u_hasFog", 0);
}

} // namespace Vkm::Engine
