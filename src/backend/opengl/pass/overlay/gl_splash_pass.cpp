#include "pass/overlay/gl_splash_pass.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

#include <GL/glew.h>

#include "vkm_opengl/core/context.h"
#include "vkm_opengl/shader/shader.h"
#include "vkm_opengl/target/frame_buffer.h"
#include "vkm_opengl/texture/texture.h"

#include "gl_screen_triangle.h"

#include "pass/gl_frame_context.h"
#include "gl_image_upload.h"
#include "convention/gl_bindings.h"
#include "debug/error/engine_error_log.h"
#include "base/profile/profiler.h"
#include "vkm_platform/threading/thread_pool.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

namespace {

// How much of each screen axis a logo may span.
constexpr float LOGO_EXTENT = 0.42f;
// The caption's line: its height, its widest and its gap to the screen's foot, as shares of the screen.
constexpr float CAPTION_HEIGHT = 0.02f;
constexpr float CAPTION_WIDEST = 0.9f;
constexpr float CAPTION_FOOT   = 0.04f;

// An ident's logo: how tall it stands, as a share of the screen's height.
constexpr float IDENT_HEIGHT = 0.3f;

constexpr uint8_t INSIDE_ALPHA = 128;  ///< The coverage at or above which a texel is inside an image's shape,
                                       ///< of 255.

constexpr float UNREACHED = 1e20f;  ///< What a cell no distance is measured from costs: far past any image,
                                    ///< finite so sums stay numbers.

constexpr int   GLOW_SCALE = 8;      ///< The glow is worked out from the logo's coverage at a GLOW_SCALE-th
                                     ///< of its size.
constexpr int   GLOW_PAD   = 48;     ///< Texels of that size padded on each side, so the glow spreads past
                                     ///< the image's edge.
constexpr float GLOW_NEAR  = 2.5f;   ///< Gaussian width, in those texels, of the halo the hot logo has.
constexpr float GLOW_WIDE  = 10.0f;  ///< Gaussian width, in those texels, of the light it sheds.

/**
 * @brief The squared distance from each cell of a line to the nearest cell it is measured from, by
 *        the lower envelope of parabolas (Felzenszwalb and Huttenlocher), written over @p cost.
 *
 * @param cost  One line: 0 at the cells measured from, UNREACHED elsewhere, or a column pass's
 *              squared distances; each cell's squared distance on return.
 * @param count Its cells.
 * @param work  Scratch: centers, edges and results, sized for the longest line.
 */
void distanceAlong(float* cost, int count, std::vector<float>& work) {
    const auto at = [&](size_t i) -> float& { return work[i]; };
    const size_t centers = 0;
    const size_t edges   = static_cast<size_t>(count);
    const size_t result  = edges + static_cast<size_t>(count) + 1;
    const auto meet = [&](int q, int p) {
        const float fq = cost[q] + static_cast<float>(q) * static_cast<float>(q);
        const float fp = cost[p] + static_cast<float>(p) * static_cast<float>(p);
        return (fq - fp) / static_cast<float>(2 * (q - p));
    };
    int k = 0;
    at(centers)   = 0.0f;
    at(edges)     = -std::numeric_limits<float>::infinity();
    at(edges + 1) = std::numeric_limits<float>::infinity();
    for (int q = 1; q < count; ++q) {
        float cross = meet(q, static_cast<int>(at(centers + static_cast<size_t>(k))));
        while (cross <= at(edges + static_cast<size_t>(k))) {
            --k;
            cross = meet(q, static_cast<int>(at(centers + static_cast<size_t>(k))));
        }
        ++k;
        at(centers + static_cast<size_t>(k))   = static_cast<float>(q);
        at(edges + static_cast<size_t>(k))     = cross;
        at(edges + static_cast<size_t>(k) + 1) = std::numeric_limits<float>::infinity();
    }
    k = 0;
    for (int q = 0; q < count; ++q) {
        while (at(edges + static_cast<size_t>(k) + 1) < static_cast<float>(q)) ++k;
        const int   p = static_cast<int>(at(centers + static_cast<size_t>(k)));
        const float d = static_cast<float>(q - p);
        at(result + static_cast<size_t>(q)) = d * d + cost[p];
    }
    for (int q = 0; q < count; ++q) cost[q] = at(result + static_cast<size_t>(q));
}

/**
 * @brief The exact distance, in texels, from each texel of @p image to the nearest whose inside
 *        is not @p inside: columns, then rows.
 *
 * @param image  The decoded image.
 * @param inside Whether the distance is from texels inside its shape (to the nearest outside).
 * @return width * height distances, bottom-up as the image's rows.
 */
std::vector<float> distanceToEdge(const DecodedImage& image, bool inside) {
    const int width  = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    std::vector<float> field(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (size_t i = 0; i < field.size(); ++i) {
        const bool in = image.pixels[i * 4 + 3] >= INSIDE_ALPHA;
        field[i] = in == inside ? UNREACHED : 0.0f;
    }
    const auto longest = static_cast<size_t>(std::max(width, height));
    std::vector<float> line(longest);
    std::vector<float> work(3 * longest + 1);
    for (int x = 0; x < width; ++x) {
        const auto cell = [&](int y) -> float& { return field[static_cast<size_t>(y * width + x)]; };
        for (int y = 0; y < height; ++y) line[static_cast<size_t>(y)] = cell(y);
        distanceAlong(line.data(), height, work);
        for (int y = 0; y < height; ++y) cell(y) = line[static_cast<size_t>(y)];
    }
    for (int y = 0; y < height; ++y) {
        distanceAlong(field.data() + static_cast<size_t>(y) * static_cast<size_t>(width), width, work);
    }
    for (float& d : field) d = std::sqrt(d);
    return field;
}

/**
 * @brief @p field, @p width by @p height, blurred by a Gaussian @p sigma texels wide, rows then
 *        columns; past its edges it is taken as empty.
 *
 * @param field  The values, row by row.
 * @param width  Texels a row.
 * @param height Rows.
 * @param sigma  The Gaussian's width, in texels.
 * @return The blurred values.
 */
std::vector<float> gaussianBlur(const std::vector<float>& field, int width, int height, float sigma) {
    const int reach = static_cast<int>(std::ceil(3.0f * sigma));
    std::vector<float> weights(static_cast<size_t>(2 * reach + 1));
    float total = 0.0f;
    for (int i = -reach; i <= reach; ++i) {
        const float w = std::exp(-0.5f * static_cast<float>(i * i) / (sigma * sigma));
        weights[static_cast<size_t>(i + reach)] = w;
        total += w;
    }
    for (float& w : weights) w /= total;

    const auto at = [width](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(width) + x; };
    std::vector<float> rows(field.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int i = std::max(-reach, -x); i <= std::min(reach, width - 1 - x); ++i) {
                sum += weights[static_cast<size_t>(i + reach)] * field[at(x + i, y)];
            }
            rows[at(x, y)] = sum;
        }
    }
    std::vector<float> blurred(field.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int i = std::max(-reach, -y); i <= std::min(reach, height - 1 - y); ++i) {
                sum += weights[static_cast<size_t>(i + reach)] * rows[at(x, y + i)];
            }
            blurred[at(x, y)] = sum;
        }
    }
    return blurred;
}

/**
 * @brief The glow of @p image: its coverage at a GLOW_SCALE-th of its size, padded by GLOW_PAD,
 *        blurred to GLOW_NEAR (red) and GLOW_WIDE (green), so it fades out well past the image.
 *
 * @param image The decoded image.
 * @param size  Receives the glow's texels across and up.
 * @param rect  Receives what takes the image's uv to the glow's: xy a scale, zw an offset.
 * @return The glow, two floats a texel.
 */
std::vector<float> glowOf(const DecodedImage& image, glm::ivec2& size, glm::vec4& rect) {
    const int inW    = (static_cast<int>(image.width) + GLOW_SCALE - 1) / GLOW_SCALE;
    const int inH    = (static_cast<int>(image.height) + GLOW_SCALE - 1) / GLOW_SCALE;
    const int width  = inW + 2 * GLOW_PAD;
    const int height = inH + 2 * GLOW_PAD;
    std::vector<float> coverage(static_cast<size_t>(width) * static_cast<size_t>(height), 0.0f);
    for (int y = 0; y < static_cast<int>(image.height); ++y) {
        for (int x = 0; x < static_cast<int>(image.width); ++x) {
            const size_t texel = static_cast<size_t>(y) * image.width + static_cast<size_t>(x);
            const size_t cell  = static_cast<size_t>(y / GLOW_SCALE + GLOW_PAD) * static_cast<size_t>(width)
                + static_cast<size_t>(x / GLOW_SCALE + GLOW_PAD);
            coverage[cell] += static_cast<float>(image.pixels[texel * 4 + 3]) / 255.0f;
        }
    }
    for (float& c : coverage) c /= static_cast<float>(GLOW_SCALE * GLOW_SCALE);
    const std::vector<float> tight = gaussianBlur(coverage, width, height, GLOW_NEAR);
    const std::vector<float> wide  = gaussianBlur(coverage, width, height, GLOW_WIDE);
    std::vector<float> glow(coverage.size() * 2);
    for (size_t i = 0; i < coverage.size(); ++i) {
        glow[i * 2]     = tight[i];
        glow[i * 2 + 1] = wide[i];
    }

    // The image's uv lands on its cells, which start GLOW_PAD in and span its size at GLOW_SCALE.
    const glm::vec2 cells(static_cast<float>(width), static_cast<float>(height));
    const glm::vec2 inside(
        static_cast<float>(image.width) / static_cast<float>(GLOW_SCALE),
        static_cast<float>(image.height) / static_cast<float>(GLOW_SCALE)
    );
    rect = glm::vec4(inside / cells, glm::vec2(static_cast<float>(GLOW_PAD)) / cells);
    size = glm::ivec2(width, height);
    return glow;
}

/**
 * @brief The shape of @p image: how far inside it each texel is, in the image's heights, below zero
 *        outside it - a signed distance field, so its bevel and edge are smooth at any size.
 *
 * @param image The decoded image.
 * @return The field, one float a texel.
 */
std::vector<float> shapeOf(const DecodedImage& image) {
    const std::vector<float> in  = distanceToEdge(image, true);
    const std::vector<float> out = distanceToEdge(image, false);
    std::vector<float> shape(in.size());
    const float perHeight = 1.0f / static_cast<float>(image.height);
    // Measured between texel centers, so the edge itself lies half a texel out.
    for (size_t i = 0; i < shape.size(); ++i) {
        shape[i] = (in[i] > 0.0f ? in[i] - 0.5f : 0.5f - out[i]) * perHeight;
    }
    return shape;
}

} // namespace

GLSplashPass::GLSplashPass()
    : m_shader("shaders/opengl/overlay/splash") {}

GLSplashPass::~GLSplashPass() = default;

void GLSplashPass::adopt(SplashImage& image, const std::string& key, bool shaped) {
    image     = SplashImage{};
    image.key = key;
    if (key.empty()) return;

    // Shared, as the pool copies its tasks; the worker is handed values alone, so a replaced
    // image's making runs on into a result nobody takes.
    const auto making = std::make_shared<std::packaged_task<MadeImage()>>([key, shaped] {
        PROFILE_SCOPE("Splash/MakeImage");
        MadeImage made;
        made.decoded = decodeImageRGBA(key);
        if (shaped && made.decoded.isValid()) {
            made.shape = shapeOf(made.decoded);
            made.glow  = glowOf(made.decoded, made.glowSize, made.glowRect);
        }
        return made;
    });
    image.made = making->get_future();
    Platform::ThreadPool::get().addTask([making] { (*making)(); });
}

void GLSplashPass::land(SplashImage& image) {
    if (!image.made.valid()) return;
    if (image.made.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const MadeImage made = image.made.get();
    if (made.decoded.isValid()) image.texture = uploadImage(made.decoded, image.key);
    if (!image.texture) {
        // A project's splash entry is its author's to fix; the engine's own images ship with it.
        reportError("Splash", "splash image '" + image.key + "'", "could not be decoded; it is not drawn");
        return;
    }
    if (!made.shape.empty()) {
        Vkm::OpenGL::Texture2DParams params;
        params.width           = made.decoded.width;
        params.height          = made.decoded.height;
        params.internalFormat  = GL_R16F;
        params.format          = GL_RED;
        params.type            = GL_FLOAT;
        params.generateMipmaps = true;
        params.minFilter       = Vkm::OpenGL::TextureMinFilter::LinearMipmapLinear;
        params.data            = made.shape.data();
        image.shape            = std::make_unique<Vkm::OpenGL::Texture2D>(image.key + ":shape", params);
    }
    if (!made.glow.empty()) {
        Vkm::OpenGL::Texture2DParams params;
        params.width          = made.glowSize.x;
        params.height         = made.glowSize.y;
        params.internalFormat = GL_RG16F;
        params.format         = GL_RG;
        params.type           = GL_FLOAT;
        params.minFilter      = Vkm::OpenGL::TextureMinFilter::Linear;
        params.data           = made.glow.data();
        image.glow            = std::make_unique<Vkm::OpenGL::Texture2D>(image.key + ":glow", params);
        image.glowRect        = made.glowRect;
    }
    image.aspect = static_cast<float>(made.decoded.width) / static_cast<float>(made.decoded.height);
}

bool GLSplashPass::pending() const {
    return m_logo.made.valid() || m_caption.made.valid();
}

void GLSplashPass::execute(GLFrameContext& ctx) {
    if (!ctx.view.splashShows()) {
        // Nothing needs the images while nothing is showing; a later key is made again. Freed once
        // the frames in flight are done drawing them: deleting what the GPU still reads waits on it.
        if (m_logo.key.empty() && m_caption.key.empty()) return;
        if (m_releaseIn > 0) {
            --m_releaseIn;
            return;
        }
        adopt(m_logo, {}, false);
        adopt(m_caption, {}, false);
        return;
    }
    m_releaseIn = RELEASE_FRAMES;
    const SplashFrame& splash = *ctx.view.splash;

    // Only an ident is lit, so only its logo needs the shape its bevel is read from.
    if (splash.key != m_logo.key) adopt(m_logo, splash.key, splash.reveal);
    if (splash.caption != m_caption.key) adopt(m_caption, splash.caption, false);
    land(m_logo);
    land(m_caption);

    // The whole surface, not the viewport: in the editor the viewport is only a panel.
    Vkm::OpenGL::FrameBuffer::bindDefault();
    ctx.gl.setViewport(
        0,
        0,
        static_cast<int32_t>(ctx.view.surfaceWidth),
        static_cast<int32_t>(ctx.view.surfaceHeight)
    );
    ctx.gl.setDepthTest(false);

    // Fit the logo inside LOGO_EXTENT of each axis, keeping its proportions; an ident's stands
    // IDENT_HEIGHT tall.
    const float surfaceW = static_cast<float>(std::max(ctx.view.surfaceWidth, 1u));
    const float surfaceH = static_cast<float>(std::max(ctx.view.surfaceHeight, 1u));
    const float screen   = surfaceW / surfaceH;

    float width  = LOGO_EXTENT;
    float height = LOGO_EXTENT * screen / m_logo.aspect;
    if (height > LOGO_EXTENT) {
        height = LOGO_EXTENT;
        width  = LOGO_EXTENT * m_logo.aspect / screen;
    }
    if (splash.reveal) {
        height = IDENT_HEIGHT;
        width  = IDENT_HEIGHT * m_logo.aspect / screen;
    }

    m_shader.bind();
    m_shader.setUniform4f("u_rect", 0.5f - width * 0.5f, 0.5f - height * 0.5f, width, height);
    m_shader.setUniform1f("u_opacity", splash.opacity);
    m_shader.setUniform1i("u_hasLogo", m_logo.texture ? 1 : 0);
    if (m_logo.texture) m_logo.texture->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_LOGO);
    if (m_logo.shape) m_logo.shape->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_SHAPE);
    if (m_logo.glow) m_logo.glow->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_GLOW);
    m_shader.setUniform4fv("u_glowRect", m_logo.glowRect);
    m_shader.setUniform1i("u_ident", splash.reveal ? 1 : 0);
    m_shader.setUniform1f("u_logoAspect", m_logo.aspect);
    m_shader.setUniform1f("u_time", splash.time);

    // The caption's line, centered at the foot, as high as CAPTION_HEIGHT unless that is too wide.
    const float lineW = std::min(CAPTION_HEIGHT * m_caption.aspect / screen, CAPTION_WIDEST);
    const float lineH = lineW * screen / m_caption.aspect;
    m_shader.setUniform4f("u_captionRect", 0.5f - lineW * 0.5f, CAPTION_FOOT, lineW, lineH);
    m_shader.setUniform1i("u_hasCaption", m_caption.texture ? 1 : 0);
    if (m_caption.texture) m_caption.texture->bindSlot(GLBindings::OverlayTextureSlots::SPLASH_CAPTION);

    ctx.screenTri.draw();
}

} // namespace Vkm::Engine
