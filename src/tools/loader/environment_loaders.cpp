#define VKM_LOG_CATEGORY "LOADER"

#include "loader/environment_loaders.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "logger.h"

// stb_image is header-only; STB_IMAGE_IMPLEMENTATION is defined once in
// texture_loaders.cpp (same EngineTools target), so include declarations only.
#include "stb_image.h"

namespace Engine {

namespace {
    // The editor stores asset paths relative to the project root (e.g.
    // "assets/envs/environment_v3.hdr"), but the process working directory is
    // not the project root, so a bare fopen fails ("can't fopen" -> IBL stays
    // off). Resolve relatives against APP_ROOT_DIR (the documented engine
    // convention - matches scene IO / model import). Absolute paths (the
    // main.cpp default) pass through unchanged.
    std::string resolveAssetPath(const std::string& p) {
#ifdef APP_ROOT_DIR
        std::filesystem::path fp(p);
        if (fp.is_relative()) {
            return (std::filesystem::path(APP_ROOT_DIR) / fp)
                       .lexically_normal().string();
        }
#endif
        return p;
    }

    /**
     * @brief Adobe .cube 3D LUT parser, converted to the engine's horizontal-
     *        strip layout (composite pass's existing PNG path).
     *
     * Cube format (de facto standard exported by Davinci / Photoshop / etc.):
     *   - Optional TITLE / DOMAIN_MIN / DOMAIN_MAX headers
     *   - LUT_3D_SIZE N            (typical N = 16, 32, 33, 64)
     *   - N^3 lines of "R G B" floats in [0, 1] (or [DOMAIN_MIN..DOMAIN_MAX])
     *   - R varies fastest, then G, then B (per the IRIDAS spec)
     *
     * Strip layout the composite pass expects:
     *   - width = N*N, height = N
     *   - Each N*N tile is a slice of constant B (placed left -> right by B)
     *   - Within a tile, x = R, y = G
     *
     * Returned with valid() == false on parse failure or size mismatch.
     */
    LDRImage parseCubeLUT(const std::string& resolved) {
        LDRImage image;
        std::ifstream f(resolved);
        if (!f) {
            LOG_ERROR("Failed to open .cube LUT '%s'", resolved.c_str());
            return image;
        }

        int size = 0;
        std::vector<float> triplets;
        triplets.reserve(32 * 32 * 32 * 3);  // typical worst case

        std::string line;
        while (std::getline(f, line)) {
            // Strip end-of-line comments and trim outer whitespace.
            const size_t hash = line.find('#');
            if (hash != std::string::npos) line.erase(hash);
            auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
            line.erase(line.begin(), std::find_if_not(line.begin(), line.end(), isWs));
            line.erase(std::find_if_not(line.rbegin(), line.rend(), isWs).base(), line.end());
            if (line.empty()) continue;

            if (line.rfind("LUT_3D_SIZE", 0) == 0) {
                std::sscanf(line.c_str(), "LUT_3D_SIZE %d", &size);
                continue;
            }
            // TITLE, DOMAIN_MIN, DOMAIN_MAX, LUT_1D_SIZE: skip header keywords
            if (line.rfind("TITLE",      0) == 0 ||
                line.rfind("DOMAIN_",    0) == 0 ||
                line.rfind("LUT_1D_",    0) == 0) {
                continue;
            }

            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (std::sscanf(line.c_str(), "%f %f %f", &r, &g, &b) == 3) {
                triplets.push_back(r);
                triplets.push_back(g);
                triplets.push_back(b);
            }
        }

        const std::size_t expected = static_cast<std::size_t>(size) * size * size * 3u;
        if (size <= 0 || triplets.size() != expected) {
            LOG_ERROR("Failed to parse .cube LUT '%s': expected %d^3 triplets, got %zu",
                resolved.c_str(), size, triplets.size() / 3u);
            return image;
        }

        // Build the horizontal-strip image. Iteration nests B outermost so
        // each B slice writes to one contiguous N-wide column block, with R
        // varying horizontally and G vertically inside the block. Source
        // index follows the .cube R-fastest convention.
        const int W = size * size;
        const int H = size;
        image.width  = static_cast<uint32_t>(W);
        image.height = static_cast<uint32_t>(H);
        image.pixels.resize(static_cast<std::size_t>(W) * H * 4u);

        auto quantize = [](float v) {
            int q = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            return static_cast<unsigned char>(std::clamp(q, 0, 255));
        };

        for (int b = 0; b < size; ++b) {
            for (int g = 0; g < size; ++g) {
                for (int r = 0; r < size; ++r) {
                    const std::size_t srcIdx = (static_cast<std::size_t>(b) * size + g) * size * 3u + r * 3u;
                    const int dstX = b * size + r;
                    const int dstY = g;
                    const std::size_t dstIdx = (static_cast<std::size_t>(dstY) * W + dstX) * 4u;
                    image.pixels[dstIdx + 0] = quantize(triplets[srcIdx + 0]);
                    image.pixels[dstIdx + 1] = quantize(triplets[srcIdx + 1]);
                    image.pixels[dstIdx + 2] = quantize(triplets[srcIdx + 2]);
                    image.pixels[dstIdx + 3] = 255;
                }
            }
        }

        LOG_INFO("Loaded .cube LUT '%s' (%dx%dx%d -> %dx%d strip)",
            resolved.c_str(), size, size, size, W, H);
        return image;
    }
}

HDRImage loadHDRImage(const std::string& filePath) {
    HDRImage image;
    const std::string resolved = resolveAssetPath(filePath);

    // GL texture origin is bottom-left; flip so the sky lands at v = 1, which
    // is what the equirect bake shader expects (matches texture_loaders.cpp).
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    float* data = stbi_loadf(resolved.c_str(), &width, &height, &channels, 3);

    if (!data) {
        LOG_ERROR("Failed to load HDR environment '%s': %s",
            resolved.c_str(), stbi_failure_reason());
        return image;
    }

    image.width  = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);

    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    image.pixels.assign(data, data + count);

    stbi_image_free(data);

    LOG_INFO("Loaded HDR environment '%s' (%dx%d equirect)",
        resolved.c_str(), width, height);
    return image;
}

LDRImage loadColorLUT(const std::string& filePath) {
    const std::string resolved = resolveAssetPath(filePath);

    // Dispatch by extension. The composite pass + thumbnail consume a
    // horizontal-strip image either way; .cube parsing converts the 3D
    // cube triplets into the same strip layout up front.
    std::string ext = std::filesystem::path(resolved).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".cube") {
        return parseCubeLUT(resolved);
    }

    LDRImage image;
    stbi_set_flip_vertically_on_load(true);

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(resolved.c_str(), &width, &height, &channels, 4);

    if (!data) {
        LOG_ERROR("Failed to load color LUT '%s': %s",
            resolved.c_str(), stbi_failure_reason());
        return image;
    }

    image.width  = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);

    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    image.pixels.assign(data, data + count);

    stbi_image_free(data);

    LOG_INFO("Loaded color LUT '%s' (%dx%d)", resolved.c_str(), width, height);
    return image;
}

} // namespace Engine
