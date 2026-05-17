#pragma once

#include <cstdint>

#include "gl_hdr_target.h"
#include "resource/gl_bloom.h"
#include "resource/gl_auto_exposure.h"
#include "resource/gl_gbuffer.h"
#include "resource/gl_taa.h"
#include "resource/gl_post_scratch.h"

namespace Engine {

/**
 * @brief The render graph's pool of viewport-sized transient GPU targets.
 *
 * One owner, one resize. Holds the HDR scene target, the bloom mip chain,
 * the auto-exposure metering targets, and the thin G-buffer. Passes reach
 * these through the backend (and, going forward, the RenderGraphContext)
 * rather than each owning a slice of backend state. Auto-exposure is
 * fixed-size and self-allocates lazily, so it has no resize step.
 */
class FrameResources {
    public:
        FrameResources() = default;
        ~FrameResources() = default;

        FrameResources(const FrameResources& other) = delete;
        FrameResources& operator=(const FrameResources& other) = delete;

        FrameResources(FrameResources && other) = delete;
        FrameResources& operator=(FrameResources && other) = delete;

    public:
        void resize(uint32_t width, uint32_t height) {
            m_hdr.resize(width, height);
            m_bloom.resize(width, height);
            m_gbuffer.resize(width, height);
            m_taa.resize(width, height);
            m_scratch.resize(width, height);
        }

        GLHdrTarget&          hdr()                { return m_hdr; }
        const GLHdrTarget&    hdr()          const { return m_hdr; }
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
        GLHdrTarget    m_hdr;
        GLBloom        m_bloom;
        GLAutoExposure m_autoExposure;
        GLGBuffer      m_gbuffer;
        GLTAA          m_taa;
        GLPostScratch  m_scratch;
};

} // namespace Engine
