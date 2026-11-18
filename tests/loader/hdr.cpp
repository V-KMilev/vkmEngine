// An environment's .hdr file as the sky is baked from it: every texel as the file stores it.

#include <cstdint>
#include <fstream>
#include <string>

#include "hdr_image.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

TEST("an .hdr stored flat decodes whole: every texel of every row as the file holds it") {
    // Rows written flat, not run-length encoded, at a width the reader first probes as encoded:
    // stb_image then decodes them from inside its loop, which GCC 15.2's jump threading broke
    // after the first row.
    constexpr int WIDTH = 64, HEIGHT = 32;
    const std::filesystem::path root = openProject();
    {
        std::ofstream out(root / "flat.hdr", std::ios::binary);
        out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " << HEIGHT << " +X " << WIDTH << "\n";
        // 12, 51, 128 at an exponent of 0: 0.046875, 0.19921875, 0.5.
        const uint8_t texel[4] = {12, 51, 128, 128};
        for (int i = 0; i < WIDTH * HEIGHT; ++i) out.write(reinterpret_cast<const char*>(texel), 4);
    }

    const HDRImage image = loadHDRImage("flat.hdr");
    REQUIRE(image.isValid());
    REQUIRE(image.width == WIDTH);
    REQUIRE(image.height == HEIGHT);
    int wrong = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 3) {
        const bool right = image.pixels[i] == 0.046875f && image.pixels[i + 1] == 0.19921875f
            && image.pixels[i + 2] == 0.5f;
        wrong += right ? 0 : 1;
    }
    CHECK(wrong == 0);
}
