#include "resource/gl_sss_lut.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <GL/glew.h>

#include "texture/gl_texture.h"

namespace Engine {

std::unique_ptr<Core::Texture2D> makeSSSLUT() {
    constexpr int W = 64;
    constexpr int H = 16;

    // Per-channel diffusion widths, modelled on Burley / Penner skin
    // params: red ~0.45, green ~0.20, blue ~0.08 in normalised surface
    // units. Coefficients shaped so curvature = 0 collapses to Lambert.
    constexpr float sigmaR = 0.45f;
    constexpr float sigmaG = 0.20f;
    constexpr float sigmaB = 0.08f;

    std::vector<float> pixels(static_cast<size_t>(W) * H * 4);

    for (int y = 0; y < H; ++y) {
        // Curvature 0..1 driven by the V axis. Floor at 0.05 so the
        // bottom row still gives the lit side a slight wrap without
        // collapsing all bleed at low curvature.
        const float curvature = std::max((static_cast<float>(y) + 0.5f) / H, 0.05f);
        for (int x = 0; x < W; ++x) {
            const float ndotl = (static_cast<float>(x) + 0.5f) / W * 2.0f - 1.0f;

            // Convolve Lambert with a per-channel Gaussian in angular
            // space around the surface point. The Gaussian's effective
            // width is (sigma * curvature) so a sharp surface gets a
            // narrow kernel (plain Lambert), a smooth/curved surface a
            // wide one (bleed past the terminator).
            float numR = 0.0f, numG = 0.0f, numB = 0.0f;
            float denR = 0.0f, denG = 0.0f, denB = 0.0f;
            constexpr int STEPS = 32;  // half-range; 65 taps total
            const float base = std::acos(std::clamp(ndotl, -1.0f, 1.0f));
            for (int i = -STEPS; i <= STEPS; ++i) {
                const float t = static_cast<float>(i) / STEPS * 3.14159265f;
                const float lambert = std::max(0.0f, std::cos(base + t));
                const float wR = std::exp(-t * t / (2.0f * (sigmaR * curvature) * (sigmaR * curvature)));
                const float wG = std::exp(-t * t / (2.0f * (sigmaG * curvature) * (sigmaG * curvature)));
                const float wB = std::exp(-t * t / (2.0f * (sigmaB * curvature) * (sigmaB * curvature)));
                numR += lambert * wR; denR += wR;
                numG += lambert * wG; denG += wG;
                numB += lambert * wB; denB += wB;
            }
            const int idx = (y * W + x) * 4;
            pixels[idx + 0] = numR / std::max(denR, 1e-6f);
            pixels[idx + 1] = numG / std::max(denG, 1e-6f);
            pixels[idx + 2] = numB / std::max(denB, 1e-6f);
            pixels[idx + 3] = 1.0f;
        }
    }

    Core::Texture2DParams p;
    p.width           = W;
    p.height          = H;
    p.internalFormat  = GL_RGBA32F;
    p.format          = GL_RGBA;
    p.type            = GL_FLOAT;
    p.wrapS           = Core::TextureWrap::ClampToEdge;
    p.wrapT           = Core::TextureWrap::ClampToEdge;
    p.minFilter       = Core::TextureMinFilter::Linear;
    p.magFilter       = Core::TextureMagFilter::Linear;
    p.generateMipmaps = false;
    p.data            = pixels.data();
    return std::make_unique<Core::Texture2D>("sss_preintegrated", p);
}

} // namespace Engine
