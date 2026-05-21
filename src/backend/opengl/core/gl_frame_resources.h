#pragma once

#include <cstdint>

#include "system/render/frame_resources.h"
#include "system/render/render_graph.h"
#include "system/render/render_graph_resource.h"

#include "gl_scene_target.h"
#include "resource/gl_bloom.h"
#include "resource/gl_auto_exposure.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_taa.h"
#include "resource/gl_post_scratch.h"

namespace Engine {

/**
 * @brief OpenGL implementation of the graph's transient resource pool.
 *
 * Holds the HDR scene target, the bloom mip chain, the auto-exposure
 * metering targets, the thin G-buffer, the TAA history, and the shared
 * post scratch target. resize() reallocates everything; registerWith()
 * publishes each sub-resource into the graph's typed pool keyed by
 * RGResource (so passes look up via ctx.resource<GLSceneTarget>(SceneHDR)).
 *
 * Auto-exposure is fixed-size and self-allocates lazily, so it has no
 * resize step.
 */
class GLFrameResources : public FrameResources {
    public:
        GLFrameResources() = default;
        ~GLFrameResources() override = default;

    public:
        void resize(uint32_t width, uint32_t height) override {
            m_hdr.resize(width, height);
            m_bloom.resize(width, height);
            m_gbuffer.resize(width, height);
            m_taa.resize(width, height);
            m_scratch.resize(width, height);
        }

        void registerWith(RenderGraph& graph) override {
            graph.registerResource(RGResource::SceneHDR,         &m_hdr);
            graph.registerResource(RGResource::SceneHDRResolved, &m_hdr);
            graph.registerResource(RGResource::BloomChain,       &m_bloom);
            graph.registerResource(RGResource::AdaptedLuminance, &m_autoExposure);
            graph.registerResource(RGResource::GBufferNormal,    &m_gbuffer);
            graph.registerResource(RGResource::GBufferPosition,  &m_gbuffer);
            graph.registerResource(RGResource::AO,               &m_gbuffer);
            graph.registerResource(RGResource::TAAHistory,       &m_taa);
            graph.registerResource(RGResource::PostScratch,      &m_scratch);
        }

        void resolveSceneColor() override { m_hdr.resolve(); }

        void invalidateTemporalHistory() override { m_taa.invalidateHistory(); }

        GLSceneTarget&          hdr()                { return m_hdr; }
        const GLSceneTarget&    hdr()          const { return m_hdr; }
        GLBloom&              bloom()              { return m_bloom; }
        const GLBloom&        bloom()        const { return m_bloom; }
        GLAutoExposure&       autoExposure()       { return m_autoExposure; }
        const GLAutoExposure& autoExposure() const { return m_autoExposure; }
        GLGBuffer&            gbuffer()            { return m_gbuffer; }
        const GLGBuffer&      gbuffer()      const { return m_gbuffer; }
        GLTAA&                taa()                { return m_taa; }
        const GLTAA&          taa()          const { return m_taa; }
        GLPostScratch&        scratch()            { return m_scratch; }
        const GLPostScratch&  scratch()      const { return m_scratch; }

    private:
        GLSceneTarget    m_hdr;
        GLBloom        m_bloom;
        GLAutoExposure m_autoExposure;
        GLGBuffer      m_gbuffer;
        GLTAA          m_taa;
        GLPostScratch  m_scratch;
};

} // namespace Engine
