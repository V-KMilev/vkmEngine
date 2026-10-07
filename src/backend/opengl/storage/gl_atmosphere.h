#pragma once

#include <cstdint>
#include <memory>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "system/sky/atmosphere.h"

namespace Vkm::GL {
    class Context;
    class FrameBuffer;
    class MipChainTexture;
    class ShaderBase;
    class Texture2D;
    class Texture3D;
}

namespace Vkm::Engine {

/**
 * @brief The procedural sky: its air and the sun and moon that light it.
 */
struct SkyParams {
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the sun, normalized.
    glm::vec3 sunIlluminance{0.0f};      ///< Above the air (Atmosphere::solarIlluminance).
    /// The air the sky scatters through, already under the sky's scales.
    Atmosphere::Coefficients air{};
    float     mieG         = 0.76f;      ///< Mie phase asymmetry.

    // Night: the atmosphere is nearly black with the sun down, so a night scene's light is
    // authored, a skyglow floor plus a broad moon lobe.
    glm::vec3 nightRadiance{0.0f};
    glm::vec3 moonDir{0.0f, 1.0f, 0.0f};  ///< Direction TO the moon, normalized.
    float     moonHalo = 0.0f;  ///< Radiance of the glow around the moon, not the disc itself.

    /// Cosine of the widest angle (about 0.57 degrees) two directions can be apart and bake the same.
    static constexpr float SAME_DIRECTION = 0.99995f;

    /// Cosine of the widest move (5 degrees) of the sun or moon between frames that a bake spread
    /// over frames follows; a wider one is a jump.
    static constexpr float DRIFT = 0.9962f;

    /**
     * @brief Whether two skies bake the same.
     *
     * Every term exact but the sun and moon directions, compared by angle against SAME_DIRECTION:
     * they move continuously, and an exact compare would bake every frame the sun animates.
     *
     * @param other The sky to compare against.
     * @return Whether a bake of @p other would produce this one.
     */
    bool operator==(const SkyParams& other) const { return matches(other, SAME_DIRECTION); }

    /**
     * @brief Whether this sky is @p other with its sun and moon moved on, but no further than DRIFT.
     *
     * @param other The sky to compare against.
     * @return Whether a bake spread over frames may follow the change.
     */
    bool driftsFrom(const SkyParams& other) const { return matches(other, DRIFT); }

    /**
     * @brief Whether every term is exact but the sun and moon directions, each within an angle.
     *
     * @param other  The sky to compare against.
     * @param cosine Cosine of the widest angle each direction may move.
     * @return Whether @p other is this sky, give or take its sun and moon.
     */
    bool matches(const SkyParams& other, float cosine) const {
        return sunIlluminance == other.sunIlluminance
            && air            == other.air
            && mieG           == other.mieG
            && nightRadiance  == other.nightRadiance
            && moonHalo       == other.moonHalo
            && glm::dot(sunDir, other.sunDir) >= cosine
            && glm::dot(moonDir, other.moonDir) >= cosine;
    }
};

/**
 * @brief The procedural sky's tables (Hillaire 2020).
 *
 * Two depend on the air alone and are baked when it changes (GLIBLBaker::bakeAir): the
 * transmittance from any altitude and angle to the top of the air, and the multiple scattering
 * built from it. Two follow the camera and the sun and are computed every frame
 * (GLAtmospherePass): the sky-view table the skybox draws the sky from, and the
 * aerial-perspective volume - the air's light and transmittance between the eye and each froxel
 * of the view - that shaders/fog.glsl lays over every surface. The env-cube bake reads the first
 * two.
 */
class GLAtmosphere {
    public:
        static constexpr int TRANSMITTANCE_WIDTH  = 256;  ///< Transmittance table: view angles
        static constexpr int TRANSMITTANCE_HEIGHT = 64;   ///< Transmittance table: altitudes
        static constexpr int MULTISCATTERING_SIZE = 32;   ///< Multiple-scattering table, square
        static constexpr int SKY_VIEW_WIDTH       = 192;  ///< Sky-view table: azimuths from the sun's
        static constexpr int SKY_VIEW_HEIGHT      = 108;  ///< Sky-view table: zenith angles
        /// Aerial-perspective froxels across, down and deep (Hillaire's 32).
        static constexpr int AERIAL_PERSPECTIVE_SIZE = 32;
        /// The deepest the aerial-perspective slices reach, in metres; a point beyond takes the last.
        static constexpr float AERIAL_PERSPECTIVE_REACH = 32000.0f;

        GLAtmosphere();
        ~GLAtmosphere();

        GLAtmosphere(const GLAtmosphere& other) = delete;
        GLAtmosphere& operator=(const GLAtmosphere& other) = delete;

        GLAtmosphere(GLAtmosphere && other) = delete;
        GLAtmosphere& operator=(GLAtmosphere && other) = delete;

    public:
        /**
         * @brief Set the air's extinction on @p shader: all of the air the transmittance table reads.
         *
         * @param shader The bound program.
         * @param air    The air.
         */
        static void setExtinction(const Vkm::GL::ShaderBase& shader, const Atmosphere::Coefficients& air);

        /**
         * @brief Set the air's coefficients on @p shader, as shaders/atmosphere.glsl declares them.
         *
         * @param shader The bound program.
         * @param air    The air.
         */
        static void setAir(const Vkm::GL::ShaderBase& shader, const Atmosphere::Coefficients& air);

        /**
         * @brief Set the air, its Mie phase and the sun's light on it on @p shader.
         *
         * For a program that marches the sky (integrateAir in shaders/atmosphere.glsl), which
         * declares `u_sunIlluminance` beside what the include declares.
         *
         * @param shader      The bound program.
         * @param sky         Supplies the air and its phase.
         * @param illuminance The sun's light above the air, as this program is to see it.
         */
        static void setSky(
            const Vkm::GL::ShaderBase& shader,
            const SkyParams& sky,
            const glm::vec3& illuminance
        );

        /**
         * @brief Allocate the transmittance and multiple-scattering tables and the FBO they draw through.
         *
         * Idempotent: a second call is a no-op.
         */
        void createAir();

        /**
         * @brief Allocate the sky-view table and the aerial-perspective volume.
         *
         * Idempotent, like createAir.
         */
        void createView();

        void bindFbo() const;
        void unbindFbo() const;

        /**
         * @brief Attach the transmittance table as colour 0 of the FBO, sized to its viewport.
         *
         * @param gl Live GL context whose viewport is set to the table's size.
         */
        void attachTransmittance(const Vkm::GL::Context& gl) const;

        /**
         * @brief Attach the multiple-scattering table as colour 0 of the FBO, sized to its viewport.
         *
         * @param gl Live GL context whose viewport is set to the table's size.
         */
        void attachMultiScattering(const Vkm::GL::Context& gl) const;

        // Sampler binds.
        void bindTransmittance(uint32_t slot) const;
        void bindMultiScattering(uint32_t slot) const;
        void bindSkyView(uint32_t slot) const;
        void bindAerialPerspective(uint32_t slot) const;

        // Compute image binds, for the per-frame tables' writers.
        void bindSkyViewImage(uint32_t unit, GLenum access) const;
        void bindAerialPerspectiveImage(uint32_t unit, GLenum access) const;

        /**
         * @brief Record the view depth this frame's aerial-perspective slices reach.
         *
         * @param depth Metres; the camera's far plane or AERIAL_PERSPECTIVE_REACH, the nearer.
         */
        void setAerialDepth(float depth) { m_aerialDepth = depth; }
        float aerialDepth() const { return m_aerialDepth; }

    private:
        std::unique_ptr<Vkm::GL::Texture2D>       m_transmittance;
        std::unique_ptr<Vkm::GL::Texture2D>       m_multiScattering;
        std::unique_ptr<Vkm::GL::MipChainTexture> m_skyView;
        std::unique_ptr<Vkm::GL::Texture3D>       m_aerialPerspective;
        std::unique_ptr<Vkm::GL::FrameBuffer>     m_fbo;

        float m_aerialDepth = AERIAL_PERSPECTIVE_REACH;
};

} // namespace Vkm::Engine
