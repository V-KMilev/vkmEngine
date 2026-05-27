#define VKM_LOG_CATEGORY "BACKEND::GL"

#include "gl_pass_registration.h"

#include "logger.h"

#include "resource/material_asset.h"   // MaterialType for forward-pass unlit override
#include "resource/resource_manager.h"
#include "resource/shader_asset.h"
#include "system/render/render_pass_factory.h"

#include "gl_aabb_debug_pass.h"
#include "gl_bloom_pass.h"
#include "gl_composite_pass.h"
#include "gl_dof_pass.h"
#include "gl_exposure_pass.h"
#include "gl_forward_pass.h"
#include "gl_grid_pass.h"
#include "gl_gtao_pass.h"
#include "gl_ibl_bake_pass.h"
#include "gl_hiz_pass.h"
#include "gl_lens_flare_pass.h"
#include "gl_motion_blur_pass.h"
#include "gl_oit_resolve_pass.h"
#include "gl_prepass.h"
#include "gl_shadow_pass.h"
#include "gl_skybox_pass.h"
#include "gl_ssr_pass.h"
#include "gl_taa_pass.h"

namespace Engine {

namespace {

ShaderHandle requireShader(ResourceManager& r, const char* name) {
    ShaderHandle h = r.findByName<ShaderAsset>(name);
    if (!h) {
        LOG_ERROR("GLPassRegistration: required shader '%s' not registered", name);
    }
    return h;
}

} // namespace

void registerBuiltinGLPasses() {
    auto& f = RenderPassFactory::get();

    f.registerPass("GLIBLBakePass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLIBLBakePass>(
            requireShader(r, "shader:ibl_equirect"),
            requireShader(r, "shader:ibl_irradiance"),
            requireShader(r, "shader:ibl_prefilter"),
            requireShader(r, "shader:ibl_brdf"));
    });

    f.registerPass("GLShadowPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLShadowPass>(requireShader(r, "shader:shadow"));
    });

    f.registerPass("GLPrepass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLPrepass>(requireShader(r, "shader:prepass"));
    });

    f.registerPass("GLGTAOPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLGTAOPass>(requireShader(r, "shader:gtao"));
    });

    f.registerPass("GLForwardPass.Opaque", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        auto pass = std::make_unique<GLForwardPass>(
            requireShader(r, "shader:pbr"), GLForwardPass::Phase::Opaque);
        pass->setShader(MaterialType::Unlit, requireShader(r, "shader:unlit"));
        return pass;
    });

    f.registerPass("GLSkyboxPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLSkyboxPass>(requireShader(r, "shader:skybox"));
    });

    f.registerPass("GLForwardPass.Transparent", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        auto pass = std::make_unique<GLForwardPass>(
            requireShader(r, "shader:pbr"), GLForwardPass::Phase::Transparent);
        pass->setShader(MaterialType::Unlit, requireShader(r, "shader:unlit"));
        return pass;
    });

    f.registerPass("GLAABBDebugPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLAABBDebugPass>(requireShader(r, "shader:aabb_debug"));
    });

    f.registerPass("GLGridPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLGridPass>(requireShader(r, "shader:grid"));
    });

    f.registerPass("GLSSRPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLSSRPass>(requireShader(r, "shader:ssr"));
    });

    f.registerPass("GLLensFlarePass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLLensFlarePass>(requireShader(r, "shader:lens_flare"));
    });

    f.registerPass("GLOITResolvePass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLOITResolvePass>(requireShader(r, "shader:oit_resolve"));
    });

    f.registerPass("GLHiZPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLHiZPass>(
            requireShader(r, "shader:hiz_init"),
            requireShader(r, "shader:hiz_reduce"));
    });

    f.registerPass("GLTAAPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLTAAPass>(requireShader(r, "shader:taa"));
    });

    f.registerPass("GLDofPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLDofPass>(requireShader(r, "shader:dof"));
    });

    f.registerPass("GLMotionBlurPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLMotionBlurPass>(requireShader(r, "shader:motion_blur"));
    });

    f.registerPass("GLBloomPass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLBloomPass>(
            requireShader(r, "shader:bloom_down"),
            requireShader(r, "shader:bloom_up"));
    });

    f.registerPass("GLExposurePass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLExposurePass>(
            requireShader(r, "shader:lum"),
            requireShader(r, "shader:exposure"));
    });

    f.registerPass("GLCompositePass", [](ResourceManager& r) -> std::unique_ptr<RenderPass> {
        return std::make_unique<GLCompositePass>(requireShader(r, "shader:composite"));
    });
}

} // namespace Engine
