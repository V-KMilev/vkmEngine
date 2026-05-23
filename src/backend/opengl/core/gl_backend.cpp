#include "gl_backend.h"

#include <algorithm>
#include <utility>

#include <GL/glew.h>

#include "logger.h"

#include "debug/profiler_gl.h"
#include "gl_context.h"
#include "gl_frame_resources.h"
#include "gl_shader.h"
#include "gl_texture.h"
#include "system/render/render_graph.h"
#include "system/render/render_view.h"

namespace Engine {

std::unique_ptr<FrameResources> GLBackend::createFrameResources() {
    return std::make_unique<GLFrameResources>();
}

std::unique_ptr<RenderTarget> GLBackend::createOffscreenTarget(uint32_t size) {
    if (size == 0) return nullptr;
    return std::make_unique<GLFramebufferTarget>(size, size);
}

uint32_t GLBackend::snapshotToTexture(uint32_t srcTextureId, uint64_t key,
                                      uint32_t size) {
    if (srcTextureId == 0 || size == 0) return 0;

    auto& slot = m_thumbCache[key];
    if (!slot) {
        Core::Texture2DParams p;
        p.width           = size;
        p.height          = size;
        p.internalFormat  = GL_RGBA8;
        p.format          = GL_RGBA;
        p.type            = GL_UNSIGNED_BYTE;
        p.wrapS           = Core::TextureWrap::ClampToEdge;
        p.wrapT           = Core::TextureWrap::ClampToEdge;
        p.minFilter       = Core::TextureMinFilter::Linear;
        p.magFilter       = Core::TextureMagFilter::Linear;
        p.generateMipmaps = false;
        slot = std::make_unique<Core::Texture2D>("thumb", p);
    }

    // Both textures are RGBA8 at the same size, so a straight image copy
    // is the cheapest stable snapshot.
    glCopyImageSubData(srcTextureId,  GL_TEXTURE_2D, 0, 0, 0, 0,
                       slot->getID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                       static_cast<GLsizei>(size),
                       static_cast<GLsizei>(size), 1);
    return slot->getID();
}

uint32_t GLBackend::cachedThumbnail(uint64_t key) const {
    auto it = m_thumbCache.find(key);
    return (it != m_thumbCache.end() && it->second) ? it->second->getID() : 0u;
}

void GLBackend::evictThumbnail(uint64_t key) {
    m_thumbCache.erase(key);
}

void GLBackend::clearThumbnailCache() {
    m_thumbCache.clear();
}

void GLBackend::registerPersistentResources(RenderGraph& graph) {
    // Resources whose lifetime exceeds a frame and which don't swap on
    // preview - the shadow atlas + the IBL set both live on GLView and are
    // created once at construction. Registered after FrameResources so the
    // graph can call this lazily on first execute().
    graph.registerResource(RGResource::ShadowAtlas, &m_view.getShadowAtlas());
    graph.registerResource(RGResource::IBL,         &m_view.getIBL());
}

void GLBackend::endFrame() {
    PROFILE_GPU_COLLECT();
}

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL), m_context() {
    // GLEW is initialized during Window creation (before any GL calls).
    // Logging the version + device here keeps GL queries inside the backend.
    const std::string ver = apiVersion();
    const std::string dev = deviceName();
    LOG_VERBOSE("OpenGL %s on %s", ver.empty() ? "?" : ver.c_str(),
                                    dev.empty() ? "?" : dev.c_str());

    // Tracy GPU profiler bootstrap. Must run on the thread that owns the GL
    // context (the main thread) before the first PROFILE_GPU_SCOPE; the
    // backend is constructed after Window::createWindow, so the context is
    // already live by the time we get here. No-op when VKM_PROFILER is off.
    PROFILE_GPU_CONTEXT();

    // Set default clear color (dark gray)
    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    
    // Initialize default OpenGL state
    m_context.setDefaultState();

    // Disable face culling by default (can be overridden by render passes)
    m_context.setFaceCulling(false);

    // Filter across cubemap face borders. Without this the IBL prefilter /
    // irradiance cubemaps show hard seams on glossy reflections (worst at the
    // low mip resolutions the prefilter uses). Global, immutable state.
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void GLBackend::resize(uint32_t width, uint32_t height) {
    m_context.setViewport(0, 0, width, height);
    m_defaultTarget.resize(width, height);
    // Transient pool is owned and resized by RenderGraph::onResize now.
}

void GLBackend::syncResources(const RenderView& view, const ResourceManager& resources) {
    m_view.sync(view, resources);
}

bool GLBackend::readbackPixels(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                uint32_t windowHeight,
                                std::vector<uint8_t>& outRGB) {
    if (w == 0 || h == 0) return false;
    if (y + h > windowHeight) return false;

    outRGB.resize(static_cast<size_t>(w) * h * 3);

    GLint prevPackAlignment = 4;
    GLint prevReadBuffer    = GL_BACK;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
    glGetIntegerv(GL_READ_BUFFER,    &prevReadBuffer);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // Default framebuffer; back buffer is post-render but pre-swap. GL_FRONT
    // is undefined under most modern compositors.
    glReadBuffer(GL_BACK);
    // ImGui y is top-down; GL is bottom-up. Flip the y origin here.
    glReadPixels(static_cast<GLint>(x),
                 static_cast<GLint>(windowHeight - y - h),
                 static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                 GL_RGB, GL_UNSIGNED_BYTE, outRGB.data());

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
    glReadBuffer(static_cast<GLenum>(prevReadBuffer));

    // Flip rows so callers get a top-down image (PNG-friendly).
    const size_t stride = static_cast<size_t>(w) * 3;
    for (uint32_t row = 0; row < h / 2; ++row) {
        uint8_t* a = outRGB.data() + row * stride;
        uint8_t* b = outRGB.data() + (h - 1 - row) * stride;
        for (size_t i = 0; i < stride; ++i) std::swap(a[i], b[i]);
    }
    return true;
}

std::string GLBackend::apiVersion() const {
    const GLubyte* v = glGetString(GL_VERSION);
    return v ? reinterpret_cast<const char*>(v) : std::string{};
}

std::string GLBackend::deviceName() const {
    const GLubyte* v = glGetString(GL_RENDERER);
    return v ? reinterpret_cast<const char*>(v) : std::string{};
}

void GLBackend::ensurePreviewResourceTables(const RenderView& view,
                                             const ResourceManager& resources) {
    // GLView::sync gates GPU-table uploads on type-version / drawable-count
    // deltas. A preview's tiny hand-built view (a freshly registered
    // primitive, a not-in-scene material) can slip past that heuristic, so
    // editor previews call this before syncResources to force its
    // mesh/material/textures resident before the unconditional batcher/UBO
    // rebuild inside sync().
    for (const auto& d : view.drawables) {
        m_view.ensureMesh(d.mesh, resources);
        m_view.ensureMaterial(d.material, resources);
        m_view.ensureMaterialTextures(d.material, resources);
    }
}

} // namespace Engine
