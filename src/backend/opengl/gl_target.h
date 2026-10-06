#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "gl_frame_buffer.h"

namespace Vkm::GL {
    class Context;
}

namespace Vkm::Engine {

/**
 * @brief An off-screen target: HDR colour and sampleable depth in one FBO, and for the scene the
 *        G-buffer and reflection inputs the screen-space passes read.
 *
 * The layout is fixed at construction; a Scene target gains or drops the reflection inputs as
 * resize() is told. With samples > 1 every attachment is multisample: geometry draws into it,
 * and GLResolvePass fills a single-sample target the screen-space passes sample.
 */
class GLTarget {
    public:
        /**
         * @brief What attachments this target carries.
         *
         * `Color` is for a target nothing depth-tests against: a pass needing depth samples the
         * *geometry* target's, so its own would be ~8 MB of dead weight at 1080p. `Scene` is the
         * frame's geometry target; of its reflection inputs (see Attachment), ReflectEnv is what
         * a screen-space reflection replaces where a ray finds the scene.
         */
        enum class Layout {
            Color,       ///< Colour only.
            ColorDepth,  ///< Colour + sampleable depth.
            Scene,       ///< Both, plus the G-buffer and the reflection inputs.
        };

        /**
         * @brief One image a target can carry, in the order a Scene target attaches them.
         *
         * A layout carries a prefix of this list.
         */
        enum class Attachment {
            Color,          ///< Colour 0: the lit HDR colour, or a colour-only target's image.
            Depth,          ///< The sampleable depth.
            GBuffer,        ///< Colour 1: oct view normal, authored roughness, metalness.
            ReflectWeight,  ///< Colour 2: the environment reflection's weight (rgb) and roughness (a).
            ReflectEnv,     ///< Colour 3: that weight times the environment radiance.
            Count,
        };

        /**
         * @brief Build an unallocated target of @p layout; resize() gives it storage.
         *
         * @param layout      Which attachments it will carry, for its whole life.
         * @param colorFormat The colour's sized format: HDR by default; a target holding data
         *                    rather than a picture names what its data needs.
         */
        explicit GLTarget(Layout layout, GLenum colorFormat = GL_RGBA16F);
        ~GLTarget();

        GLTarget(const GLTarget& other) = delete;
        GLTarget& operator=(const GLTarget& other) = delete;

        GLTarget(GLTarget && other) = delete;
        GLTarget& operator=(GLTarget && other) = delete;

    public:
        /**
         * @brief Allocate (or reallocate) the attachments at this size and sample count.
         *
         * A no-op when nothing changed, so a pass may call it every frame.
         *
         * @param width         Width in pixels; zero allocates nothing.
         * @param height        Height in pixels; zero allocates nothing.
         * @param samples       Per-pixel samples, clamped to what the driver holds for every
         *                      carried format; more than one makes every attachment multisample.
         * @param reflectInputs Whether a Scene target carries the reflection inputs; other
         *                      layouts ignore it.
         */
        void resize(uint32_t width, uint32_t height, uint32_t samples, bool reflectInputs);

        /**
         * @brief Allocate at this size, keeping the last sample count and attachments.
         *
         * @param width  Width in pixels; zero allocates nothing.
         * @param height Height in pixels; zero allocates nothing.
         */
        void resize(uint32_t width, uint32_t height);

        /**
         * @brief Free the attachment storage, keeping the FBO object.
         *
         * The next resize re-allocates. Leaves the default framebuffer bound.
         */
        void release();

        /**
         * @brief Bind for rendering into colour 0, the common case.
         *
         * Non-const: it mutates GL draw-buffer state.
         *
         * @param gl Live GL context whose viewport is set.
         */
        void bind(const Vkm::GL::Context& gl);

        /**
         * @brief Bind colour, and the reflection inputs while carried, at fragment outputs 0, 2 and 3.
         *
         * @param gl Live GL context whose viewport is set.
         */
        void bindForwardPass(const Vkm::GL::Context& gl);

        /**
         * @brief Bind for writing depth and the G-buffer: colour 1 is the only colour drawn.
         *
         * @param gl Live GL context whose viewport is set.
         */
        void bindGBufferPass(const Vkm::GL::Context& gl);

        /**
         * @brief Clear the whole target for a new frame: every colour attachment and the depth.
         *
         * The first pass to touch the target calls this.
         *
         * @param gl Live GL context the clear goes through.
         */
        void clearForFrame(const Vkm::GL::Context& gl);

        /**
         * @brief Bind one attachment to a texture unit for a shader to read.
         *
         * Multisample binds as GL_TEXTURE_2D_MULTISAMPLE (read through a sampler2DMS), single-sample
         * as GL_TEXTURE_2D. Nothing binds for an uncarried attachment, or before storage exists.
         *
         * @param attachment Which image.
         * @param slot       Texture unit to bind it to.
         */
        void bindTexture(Attachment attachment, uint32_t slot) const;

        /**
         * @brief Bind one attachment to an image unit, for a compute pass to read or write.
         *
         * Single-sample targets and carried attachments only; otherwise nothing binds.
         *
         * @param attachment Which image.
         * @param unit       Image unit the shader names.
         * @param access     GL_READ_ONLY, GL_WRITE_ONLY or GL_READ_WRITE.
         */
        void bindImage(Attachment attachment, uint32_t unit, GLenum access) const;

        /**
         * @brief Copy @p src's colour into this target, both viewport-sized.
         *
         * @param src The target copied from, resolved if it is multisample.
         */
        void blitColorFrom(const GLTarget& src);

        /**
         * @brief Sample count in effect (1 = single-sample).
         *
         * @return The clamped sample count.
         */
        uint32_t samples() const { return m_samples; }

        /**
         * @brief Whether this target carries the reflection inputs now.
         *
         * Only while resize() is told a pass reads them; otherwise they are neither allocated
         * nor bound for drawing.
         *
         * @return True for a Scene target whose last resize() asked for them.
         */
        bool hasReflectInputs() const { return carries(Attachment::ReflectWeight); }

    private:
        /// One attachment's storage: a texture, multisample or not. Defined in the .cpp.
        class Texture;

        static constexpr size_t ATTACHMENT_COUNT = static_cast<size_t>(Attachment::Count);

        /// Whether this target's layout carries @p attachment.
        bool carries(Attachment attachment) const;

        /// The storage behind @p attachment; null when not carried or not allocated.
        const Texture* texture(Attachment attachment) const;

        /// Detach @p attachment from the FBO, which must be bound, and free its storage.
        void detach(Attachment attachment);

        /**
         * @brief Bind the FBO to draw into @p buffers, sized to the viewport.
         *
         * @param gl      Live GL context whose viewport is set.
         * @param buffers Draw buffers, indexed by fragment output location.
         * @param count   How many of them.
         */
        void bindDrawing(const Vkm::GL::Context& gl, const GLenum* buffers, int count);

        /**
         * @brief Bind the FBO with @p buffers as its draw buffers.
         *
         * Issued only when it differs from this FBO's last set: a driver revalidates the
         * framebuffer on every change, and most binds ask for the set already there.
         *
         * @param buffers Draw buffers, indexed by fragment output location.
         * @param count   How many of them, at most four.
         */
        void bindDrawBuffers(const GLenum* buffers, int count);

    private:
        uint32_t m_width   = 0;
        uint32_t m_height  = 0;
        uint32_t m_samples = 1;
        uint32_t m_requestedSamples = 1;  ///< What resize() was last asked for, before the clamp.
        bool     m_reflectInputs = false;
        Layout   m_layout;
        GLenum   m_colorFormat;

        Vkm::GL::FrameBuffer m_fbo;
        std::array<GLenum, 4> m_drawBuffers{};  ///< The FBO's draw-buffer set, as last issued.
        int                   m_drawBufferCount = -1;  ///< Its length; -1 until the first is issued.

        std::array<std::unique_ptr<Texture>, ATTACHMENT_COUNT> m_textures;
};

} // namespace Vkm::Engine
