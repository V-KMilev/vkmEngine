#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "frame/draw/gl_instance_batcher.h"
#include "frame/draw/gl_occlusion_cull.h"

namespace Vkm::OpenGL {
    class Context;
}

namespace Vkm::Engine {

class GLScreenTriangle;
class GLMesh;
struct RenderView;
class GLAssetMirror;
class GLTarget;
class GLShadowAtlas;
class GLShadowPlan;
class GLIBL;
class GLLightmaps;
class GLAtmosphere;
class GLBloom;
class GLClusterGrid;
class GLFogVolume;
class GLIrradianceVolume;
class GLSkinPalette;
class GLObjectBuffer;
class GLWaveFields;
class GLWaterShore;
struct SkyParams;

/// Water whose waterlines a frame foams: where it rests and its share of the waves; no waterline
/// for none.
struct WaterShore {
    float level     = 0.0f;  ///< World height it rests at.
    float waterline = 0.0f;  ///< Meters of foam where it meets what stands in it (Water::waterline).
    float waves     = 0.0f;  ///< Its share of the waves (Water::waves).
};

/**
 * @brief Everything a GLPass needs for one frame.
 *
 * Built by the backend each frame. What a pass reads or leaves for a later one is a field here,
 * so GLPass::execute takes nothing else.
 */
struct GLFrameContext {
    const RenderView&     view;          ///< This frame's scene snapshot.
    const GLAssetMirror&  mirror;        ///< GPU mirror of the assets the frame uses.
    Vkm::OpenGL::Context& gl;            ///< GL state manager (viewport / depth / clear).
    GLScreenTriangle&     screenTri;     ///< Attribute-less fullscreen triangle.
    const GLMesh&         unitCube;      ///< The [-0.5, 0.5] unit cube.
    GLTarget&             sceneHDR;      ///< Single-sample resolved scene.
    GLTarget&             sceneRender;   ///< Where geometry draws: the multisample target, or sceneHDR when
                                         ///< MSAA is off.
    GLShadowAtlas&        shadowAtlas;   ///< The shadow depth atlas.
    GLShadowPlan&         shadowPlan;    ///< This frame's shadow plan (matrices + slots); its caster cull
                                         ///< runs on until the shadow pass joins it.
    const GLIBL&          ibl;           ///< Baked IBL product set (see GLPass::bindAmbient).
    GLAtmosphere&         atmosphere;    ///< The procedural sky's tables (see GLAtmospherePass).
    GLBloom&              bloom;         ///< Bloom mip chain.
    GLTarget&             ao;            ///< GTAO factor + octahedral bent normal.
    GLClusterGrid&        clusters;      ///< Forward+ per-cluster light lists.
    GLFogVolume&          fog;           ///< Froxel fog volumes, read through GLPass::bindFog.
    GLIrradianceVolume&   irradiance;    ///< Baked SH irradiance volume (see GLPass::bindAmbient).
    GLSkinPalette&        skinPalette;   ///< Every skinned item's bone palette, uploaded once per frame.
    const GLObjectBuffer& objects;       ///< Every object's model (and first bone), uploaded once per frame.
    GLWaveFields&         waveFields;    ///< The swell and the ripples, for the water's pixels.
    GLWaterShore&         waterShore;    ///< Water's shore, for its waterline foam.

    const std::vector<uint32_t>& alphaMask;  ///< The camera's objects by draw bucket, as indices into
                                             ///< RenderView::objects, split once per frame with one material
                                             ///< resolve each; the opaque bucket arrives batched as
                                             ///< opaqueBatch.
    const std::vector<uint32_t>& transparent;

    const GLInstanceBatcher& opaqueBatch;  ///< The opaque bucket batched once into instanced runs, as more
                                           ///< than one pass draws it. Const: a rebuild would invalidate runs
                                           ///< another pass is about to draw; a pass batching its own keeps a
                                           ///< batcher.

    // The fields below are filled by the backend before the pass loop runs.

    bool continues = false;  ///< The last frame drew this world as the eye saw it, so a pass may read again
                             ///< what it worked out then (GLRateCap); not after one that drew the chrome
                             ///< alone or a warming slice, nor once the world or its assets were replaced.

    bool waterShown = false;  ///< A drawn surface is water (ObjectDraw::water): the ripple fields are drawn
                              ///< for it, and the water program is given them, whether or not any takes a
                              ///< share of the waves.

    WaterShore shore{};  ///< The water whose shore is captured (GLWaterShore), at one level a frame: the
                         ///< nearest drawn water with a waterline (Water::waterline). Every water at its
                         ///< level foams there.

    GLOcclusionCull* occlusion = nullptr;  ///< The GPU occlusion cull of opaqueBatch this frame, or null when
                                           ///< it is off. The depth prepass runs its phases and draws its
                                           ///< first two sets; then every draw of the opaque batch is its
                                           ///< Set::Shown (drawOpaque()).

    /**
     * @brief Submit one of opaqueBatch's draws: from @p set of the cull while one runs, else whole.
     *
     * @param draw One of opaqueBatch's draws.
     * @param set  Which of the cull's lists; the forward pass's by default.
     */
    void drawOpaque(const InstanceDraw& draw, GLOcclusionCull::Set set = GLOcclusionCull::Set::Shown) const {
        if (occlusion) {
            occlusion->draw(gl, draw, set);
        } else {
            opaqueBatch.draw(gl, draw);
        }
    }

    GLTarget* colorSrc = nullptr;  ///< The scene as of the last post pass, sceneHDR at first: what a pass
                                   ///< samples.
    GLTarget* colorDst = nullptr;  ///< The free scratch it draws into; flipColor() swaps the two, so no pass
                                   ///< samples its own target.

    GLTarget* scratchA = nullptr;  ///< The two color-only scratch targets flipColor() alternates between.
                                   ///< Also transient copy space for a pass not yet in the chain.
    GLTarget* scratchB = nullptr;

    /**
     * @brief Publish colorDst as the current scene and aim colorDst at the other scratch.
     */
    void flipColor() {
        colorSrc = colorDst;
        colorDst = (colorDst == scratchA) ? scratchB : scratchA;
    }

    int probeCount = 0;  ///< How many reflection probes the backend bound this frame. Boxes and layers are in
                         ///< the ProbeBlock UBO, cubes in the probe arrays.

    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the sun, from the Environment's sun angles. Set
                                         ///< whether or not the frame has a directional light.

    float exposure = 0.0f;  ///< The stops the frame is exposed by (GLExposure::stops): a pass that scales by
                            ///< the exposure reads this, never RenderSettings::exposure.

    const SkyParams* sky = nullptr;  ///< The procedural sky this frame shows, lit by its sun. Null under an
                                     ///< HDR sky or none, which have no atmosphere.

    const GLLightmaps* lightmaps = nullptr;  ///< The fixed meshes' lightmaps (GLLightmaps): its pages are
                                             ///< bound by the pass that reads them.

    // The fields below are pass products: set by an earlier pass, read by later ones.

    bool aoReady = false;  ///< Set by the GTAO pass when it fills the AO target. Unset, the AO target must
                           ///< not be sampled.

    bool bloomReady = false;  ///< Set by the bloom pass when it fills the bloom chain. Read instead of
                              ///< re-deriving the bloom pass's own conditions.

    bool fogReady = false;  ///< Set by the fog compute when it fills and integrates the froxel volume.
                            ///< GLPass::bindFog binds the volume only when set, so no pass samples a stale
                            ///< one if its conditions and the fog compute's drift apart.

    bool skyViewReady = false;  ///< Set by the atmosphere pass when it computes the sky-view table. The
                                ///< skybox then draws the procedural sky from it rather than from the env
                                ///< cube.

    bool aerialPerspectiveReady = false;  ///< Set by the atmosphere pass when it computes the
                                          ///< aerial-perspective volume. GLPass::bindFog binds the volume
                                          ///< only when set, as it does the fog's.

    const GLTarget* upscaleSource = nullptr;  ///< Set by the composite pass when the scene is smaller than
                                              ///< the viewport: the frame it left at the scene's size,
                                              ///< display-encoded and not dithered, for GLUpscalePass. Null
                                              ///< when the composite wrote the window itself.
};

} // namespace Vkm::Engine
