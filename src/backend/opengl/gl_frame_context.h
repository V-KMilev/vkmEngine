#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "frame/gl_instance_batcher.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

class ScreenTriangle;
class GLMesh;
struct RenderView;
class GLView;
class GLTarget;
class GLShadowAtlas;
class GLShadowData;
class GLIBL;
class GLAtmosphere;
class GLBloom;
class GLClusterGrid;
class GLFogVolume;
class GLIrradianceVolume;
class GLSkinPalette;
class GLObjectBuffer;
struct SkyParams;

/**
 * @brief Everything a GLPass needs for one frame.
 *
 * Built by the backend each frame. What a pass reads or leaves for a later one is a field here,
 * so GLPass::execute takes nothing else.
 */
struct GLFrameContext {
    const RenderView&  view;             ///< This frame's scene snapshot.
    const GLView&      resources;        ///< GPU mirror of the assets the frame uses.
    Vkm::GL::Context&  gl;               ///< GL state manager (viewport / depth / clear).
    ScreenTriangle&    screenTri;        ///< Attribute-less fullscreen triangle.
    const GLMesh&      unitCube;         ///< The [-0.5, 0.5] unit cube.
    GLTarget&          sceneHDR;         ///< Single-sample resolved scene.
    /// Where geometry draws: the multisample target, or sceneHDR when MSAA is off.
    GLTarget&          sceneRender;
    GLShadowAtlas&     shadowAtlas;      ///< The shadow depth atlas.
    const GLShadowData& shadowData;      ///< This frame's shadow plan (matrices + slots).
    const GLIBL&       ibl;              ///< Baked IBL product set (see GLPass::bindAmbient).
    GLAtmosphere&      atmosphere;       ///< The procedural sky's tables (see GLAtmospherePass).
    GLBloom&           bloom;            ///< Bloom mip chain.
    GLTarget&          ao;               ///< GTAO factor + octahedral bent normal.
    GLClusterGrid&     clusters;         ///< Forward+ per-cluster light lists.
    GLFogVolume&       fog;              ///< Froxel fog volumes, read through GLPass::bindFog.
    GLIrradianceVolume& irradiance;      ///< Baked SH irradiance volume (see GLPass::bindAmbient).
    GLSkinPalette&     skinPalette;      ///< Every skinned item's bone palette, uploaded once per frame.
    const GLObjectBuffer& objects;       ///< Every object's model (and first bone), uploaded once per frame.

    /**
     * @brief The camera's objects by draw bucket, as indices into RenderView::objects.
     *
     * Split once per frame, one material resolve each. The opaque bucket arrives batched as
     * `opaqueBatch` below.
     */
    const std::vector<uint32_t>& alphaMask;
    const std::vector<uint32_t>& transparent;

    /**
     * @brief The opaque bucket already batched into instanced runs.
     *
     * More than one pass draws the identical opaque list, so it is batched once. Const: a rebuild
     * would invalidate runs another pass is about to draw. A pass batching its own keeps a batcher.
     */
    const GLInstanceBatcher& opaqueBatch;

    // The fields below are filled by the backend before the pass loop runs.

    /**
     * @brief The post-processing colour chain.
     *
     * colorSrc holds the scene as of the last pass (starting at sceneHDR); colorDst is the free
     * scratch. A post pass samples colorSrc, draws into colorDst, then calls flipColor(). After
     * the first flip it ping-pongs between the scratches, so no pass samples its own target.
     */
    GLTarget* colorSrc = nullptr;
    GLTarget* colorDst = nullptr;

    /**
     * @brief The two colour-only scratch targets flipColor() alternates between.
     *
     * Also transient copy space for a pass not yet in the chain.
     */
    GLTarget* scratchA = nullptr;
    GLTarget* scratchB = nullptr;

    /**
     * @brief Publish colorDst as the current scene and aim colorDst at the other scratch.
     */
    void flipColor() {
        colorSrc = colorDst;
        colorDst = (colorDst == scratchA) ? scratchB : scratchA;
    }

    /**
     * @brief How many reflection probes the backend bound this frame.
     *
     * Boxes and layers are in the ProbeBlock UBO, cubes in the probe arrays.
     */
    int probeCount = 0;

    /**
     * @brief Direction TO the sun, from the Environment's sun angles.
     *
     * Set whether or not the frame has a directional light.
     */
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};

    /**
     * @brief The procedural sky this frame shows, lit by its sun.
     *
     * Null under an HDR sky or none, which have no atmosphere.
     */
    const SkyParams* sky = nullptr;

    // The fields below are pass products: set by an earlier pass, read by later ones.

    /**
     * @brief Set by the GTAO pass when it fills the AO target.
     *
     * Unset, the AO target must not be sampled.
     */
    bool aoReady = false;

    /**
     * @brief Set by the bloom pass when it fills the bloom chain.
     *
     * Read instead of re-deriving the bloom pass's own conditions.
     */
    bool bloomReady = false;

    /**
     * @brief Set by the fog compute when it fills and integrates the froxel volume.
     *
     * GLPass::bindFog binds the volume only when set, so no pass samples a stale one if its
     * conditions and the fog compute's drift apart.
     */
    bool fogReady = false;

    /**
     * @brief Set by the atmosphere pass when it computes the sky-view table.
     *
     * The skybox then draws the procedural sky from it rather than from the env cube.
     */
    bool skyViewReady = false;

    /**
     * @brief Set by the atmosphere pass when it computes the aerial-perspective volume.
     *
     * GLPass::bindFog binds the volume only when set, as it does the fog's.
     */
    bool aerialPerspectiveReady = false;
};

} // namespace Vkm::Engine
