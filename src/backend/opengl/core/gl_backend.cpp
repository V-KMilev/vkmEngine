#include "gl_backend.h"

#include "logger.h"

#include <GL/glew.h>

#include "gl_context.h"
#include "gl_shader.h"
#include "gl_texture.h"

#include "system/render/render_view.h"

namespace Engine {

void GLBackend::beginPreview(uint32_t size) {
    if (size == 0) return;
    if (!m_previewTarget || m_previewSize != size) {
        m_previewTarget = std::make_unique<GLFramebufferTarget>(size, size);
        m_previewFrame.resize(size, size);
        m_previewSize = size;
    }
    m_previewMode = true;
}

uint32_t GLBackend::previewColorTexture() const {
    return m_previewTarget ? m_previewTarget->getColorTexture() : 0u;
}

uint32_t GLBackend::snapshotPreviewToCache(uint64_t key, uint32_t size) {
    if (!m_previewTarget || size == 0) return 0;
    const GLuint src = m_previewTarget->getColorTexture();
    if (!src) return 0;

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

    // The preview target and the cache slot are both RGBA8 at the same size,
    // so a straight image copy is the cheapest stable snapshot.
    glCopyImageSubData(src,            GL_TEXTURE_2D, 0, 0, 0, 0,
                       slot->getID(),  GL_TEXTURE_2D, 0, 0, 0, 0,
                       static_cast<GLsizei>(size),
                       static_cast<GLsizei>(size), 1);
    return slot->getID();
}

uint32_t GLBackend::cachedPreview(uint64_t key) const {
    auto it = m_thumbCache.find(key);
    return (it != m_thumbCache.end() && it->second) ? it->second->getID() : 0u;
}

GLBackend::GLBackend() : RenderBackend(RenderBackendType::OpenGL), m_context() {
    // GLEW is initialized during Window creation (before any GL calls)

    // Set default clear color (dark gray)
    m_context.setClearColor({0.1f, 0.1f, 0.1f, 1.0f});
    
    // Initialize default OpenGL state
    m_context.setDefaultState();
    
    // Disable face culling by default (can be overridden by render passes)
    m_context.setFaceCulling(false);
}

void GLBackend::resize(uint32_t width, uint32_t height) {
    m_context.setViewport(0, 0, width, height);
    m_defaultTarget.resize(width, height);
    m_frame.resize(width, height);
}

void GLBackend::setWireframe(bool enabled) {
    m_context.setPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
}

void GLBackend::syncResources(const RenderView& view, const ResourceManager& resources) {
    // GLView::sync gates GPU-table uploads on type-version / drawable-count
    // deltas. A preview's tiny hand-built view (a freshly registered
    // primitive, a not-in-scene material) can slip past that heuristic, so
    // force its mesh/material resident before the unconditional batcher/UBO
    // rebuild inside sync().
    if (m_previewMode) {
        for (const auto& d : view.drawables) {
            m_view.ensureMesh(d.mesh, resources);
            m_view.ensureMaterial(d.material, resources);
            m_view.ensureMaterialTextures(d.material, resources);
        }
    }
    m_view.sync(view, resources);
}

} // namespace Engine
