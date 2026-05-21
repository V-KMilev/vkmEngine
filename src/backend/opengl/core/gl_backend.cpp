#include "gl_backend.h"

#include "logger.h"

#include <GL/glew.h>

#include "gl_context.h"
#include "gl_shader.h"
#include "gl_texture.h"
#include "gl_frame_resources.h"

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

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL), m_context() {
    // GLEW is initialized during Window creation (before any GL calls)

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
