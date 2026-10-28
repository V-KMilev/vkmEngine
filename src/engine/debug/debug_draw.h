#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "debug/debug_palette.h"
#include "ecs/entity.h"
#include "system/render/data/debug_line_data.h"

namespace Vkm::Engine {

/**
 * @brief Pulses running along a path: bright dashes fading to their tails, over a faint trail.
 *
 * The backend runs them from DebugFrame::seconds, so a path is sent once a frame however many
 * pulses it carries.
 */
struct DebugPulse {
    float dash    = 0.6f;   ///< Metres one pulse runs, head to tail
    float spacing = 3.0f;   ///< Metres from one pulse's head to the next's
    float speed   = 4.0f;   ///< Metres a second along the path
    float phase   = 0.0f;   ///< Metres along the path the heads stand at time 0
    float trail   = 0.15f;  ///< How much of its color the path shows between pulses, 0 to 1
};

/**
 * @brief How a drawing looks: color, width, lifetime, glow, depth and category.
 *
 * A color converts to one, so `debugDraw().sphere(centre, radius, color)` is a whole call; the
 * rest chains: `DebugStyle(color).wide(3.0f).lasting(2.0f)`.
 */
struct DebugStyle {
    glm::vec4     color    = glm::vec4(0.0f);  ///< Display RGBA; zero alpha takes the category's
    float         width    = 1.5f;             ///< Points: pixels at a density of 1
    float         seconds  = 0.0f;             ///< Of the game's time it stands; 0, see DebugDraw
    float         glow     = 0.0f;             ///< Brightness of its glow; see glowing()
    DebugDepth    depth    = DebugDepth::Ghosted;
    DebugCategory category = DebugCategory::General;

    // Implicit: a color or a category is a whole style.
    DebugStyle() = default;
    DebugStyle(const glm::vec4& rgba) : color(rgba) {}
    DebugStyle(const glm::vec3& rgb) : color(rgb, 1.0f) {}
    DebugStyle(DebugPalette::Color c) : color(c.value()) {}
    DebugStyle(DebugCategory kind) : category(kind) {}

    DebugStyle& in(DebugCategory kind) {
        category = kind;
        return *this;
    }
    DebugStyle& wide(float points) {
        width = points;
        return *this;
    }
    DebugStyle& lasting(float s) {
        seconds = s;
        return *this;
    }
    DebugStyle& onTop() {
        depth = DebugDepth::OnTop;
        return *this;
    }
    DebugStyle& seenOnly() {
        depth = DebugDepth::Hidden;
        return *this;
    }

    /**
     * @brief Let the line light the frame where it is seen: @p brightness times its color on it,
     *        falling away over a few widths round it, and spread further by the bloom where bright.
     *
     * In displayed units, so it reads the same whatever the exposure; 2 to 6 is a soft halo.
     * Nothing glows while the bloom is off.
     *
     * @param brightness Times its color on the line; 0 is none.
     * @return This style.
     */
    DebugStyle& glowing(float brightness) {
        glow = brightness;
        return *this;
    }
};

/**
 * @brief Who DebugViewSystem centres on - the editor's selection: drawn in the highlight and,
 *        while detailForFocusOnly, alone in drawing their detail.
 */
struct DebugFocus {
    std::vector<EntityId> entities;  ///< Sorted by DebugDraw::setFocus
    EntityId              primary{};

    /// A light's reach, a sound's spheres, a volume's probes, an emitter's shape, a rig's axes:
    /// the focus's alone, or a scene of lamps buries itself in wire.
    bool detailForFocusOnly = false;

    bool has(EntityId id) const;
};

/**
 * @brief Immediate-mode drawing in the world: lines, shapes and pulses that anything submits
 *        each frame and the backend draws into it.
 *
 * The frame's thread draws, and so may any thread done before RenderSystem collects - the ones a
 * system forks with parallelFor, which join before it returns - each into a buffer of its own that
 * collect() merges once a frame: no buffer is written while it is read, so a buffer needs no lock;
 * a thread locks only to make its own, at its first drawing. A background task (addTask), which
 * runs on past the frame, draws nothing. A drawing stands for the frame it is made in; one made in
 * a fixed update stands until the next tick's are collected, so it neither doubles nor flickers
 * however the ticks fall; one given seconds stands that long of the game's time. A frame draws at
 * most MAX_LINES segments, dropping past it the timed first, then the tick's, then the frame's,
 * with one warning.
 *
 * Reached as FrameContext::debug and Behavior::debugDraw(). A category turned off returns at its
 * first check, before any segment is built.
 */
class DebugDraw {
    public:
        /// The most segments a frame draws.
        static constexpr uint32_t MAX_LINES = 1u << 16;

        /// Straight pieces in a full circle.
        static constexpr int CIRCLE_SEGMENTS = 32;

        /// A point is a dot this many of its style's widths across.
        static constexpr float POINT_WIDTHS = 4.0f;

        DebugDraw();
        ~DebugDraw();

        DebugDraw(const DebugDraw& other) = delete;
        DebugDraw& operator=(const DebugDraw& other) = delete;

        DebugDraw(DebugDraw && other) = delete;
        DebugDraw& operator=(DebugDraw && other) = delete;

    public:
        void line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style = {});

        /// Each point joined to the next; @p closed joins the last to the first.
        void polyline(
            const glm::vec3* points,
            size_t count,
            const DebugStyle& style = {},
            bool closed = false
        );
        void polyline(
            const std::vector<glm::vec3>& points,
            const DebugStyle& style = {},
            bool closed = false
        ) {
            polyline(points.data(), points.size(), style, closed);
        }

        /// A round dot, POINT_WIDTHS of @p style's widths across.
        void point(const glm::vec3& at, const DebugStyle& style = {});

        /// A shaft from @p from to @p to and a head at @p to, a fifth of its length.
        void arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style = {});

        /// The circle round @p centre facing @p normal.
        void circle(
            const glm::vec3& centre,
            const glm::vec3& normal,
            float radius,
            const DebugStyle& style = {}
        );

        /// centre + radius (cos t axisA + sin t axisB) round a full turn: an ellipse for unequal axes.
        void circle(
            const glm::vec3& centre,
            const glm::vec3& axisA,
            const glm::vec3& axisB,
            float radius,
            const DebugStyle& style = {}
        );

        /// The same from angle @p from to @p to, radians.
        void arc(
            const glm::vec3& centre,
            const glm::vec3& axisA,
            const glm::vec3& axisB,
            float radius,
            float from,
            float to,
            const DebugStyle& style = {}
        );

        /// Three great circles, one on each world plane.
        void sphere(const glm::vec3& centre, float radius, const DebugStyle& style = {});

        /// The box of @p halfExtents round @p centre, turned by @p rotation.
        void box(
            const glm::vec3& centre,
            const glm::vec3& halfExtents,
            const glm::quat& rotation,
            const DebugStyle& style = {}
        );
        void box(const glm::vec3& centre, const glm::vec3& halfExtents, const DebugStyle& style = {}) {
            box(centre, halfExtents, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), style);
        }
        /// A color spelled out, as glm would take an RGB one for a rotation and an RGBA one for RGB.
        void box(const glm::vec3& centre, const glm::vec3& halfExtents, const glm::vec3& color) {
            box(centre, halfExtents, DebugStyle(color));
        }
        void box(const glm::vec3& centre, const glm::vec3& halfExtents, const glm::vec4& color) {
            box(centre, halfExtents, DebugStyle(color));
        }

        /// The world-aligned box from @p min to @p max.
        void bounds(const glm::vec3& min, const glm::vec3& max, const DebugStyle& style = {}) {
            box((min + max) * 0.5f, (max - min) * 0.5f, style);
        }

        /// The capsule @p radius round the segment from @p a to @p b.
        void capsule(const glm::vec3& a, const glm::vec3& b, float radius, const DebugStyle& style = {});

        /// The same round @p centre along @p rotation's +Y, so its wires turn with the rotation.
        void capsule(
            const glm::vec3& centre,
            const glm::quat& rotation,
            float radius,
            float halfHeight,
            const DebugStyle& style = {}
        );

        /// A cone from @p apex along @p axis - its length the cone's - opening @p halfAngle radians.
        void cone(
            const glm::vec3& apex,
            const glm::vec3& axis,
            float halfAngle,
            const DebugStyle& style = {}
        );

        /// The volume @p invViewProjection maps glm's clip cube to: a camera's frustum.
        void frustum(const glm::mat4& invViewProjection, const DebugStyle& style = {});

        /// @p frame's three axes from its origin, @p length long, in Math::AXIS_COLORS.
        void axes(const glm::mat4& frame, float length, const DebugStyle& style = {});

        /// Pulses running along @p path; see DebugPulse.
        void pulse(
            const glm::vec3* path,
            size_t count,
            const DebugStyle& style = {},
            const DebugPulse& pulse = {}
        );
        void pulse(
            const std::vector<glm::vec3>& path,
            const DebugStyle& style = {},
            const DebugPulse& pulse = {}
        ) {
            this->pulse(path.data(), path.size(), style, pulse);
        }

        // What is shown: the host's - the editor's View > Show, or a game run without it.
        void setShown(DebugCategory category, bool shown);
        bool shown(DebugCategory category) const;

        /// The focus DebugViewSystem centres on; its entities are sorted here.
        void setFocus(DebugFocus focus);
        const DebugFocus& focus() const { return m_focus; }

        /// The host's scale over the display's density, which widths multiply by: an editor's UI scale.
        void setPointScale(float scale) { m_pointScale = scale; }
        float pointScale() const { return m_pointScale; }

        /// A fixed update begins: what is drawn until endTick() stands until the next tick's.
        void beginTick();
        void endTick() { m_inTick.store(false, std::memory_order_relaxed); }

        /// While set nothing is drawn: a replayed tick runs a moment already drawn.
        void setMuted(bool muted) { m_muted.store(muted, std::memory_order_relaxed); }

        /**
         * @brief Merge what every thread drew into lines(): what stands for seconds, the last
         *        tick's, then this frame's.
         *
         * @param seconds    Wall seconds since the last frame: the pulses' clock.
         * @param simSeconds The game's seconds since the last frame, which the timed ones age by.
         * @param worldEpoch Scene::epoch(): a world replaced takes what was drawn in it along,
         *                   its last frame's ticks and seconds included.
         */
        void collect(float seconds, float simSeconds, uint64_t worldEpoch);

        /// The frame ends: what it drew for itself goes if nothing collected it - nothing showed it.
        void endFrame();

        /// The frame's segments, after collect().
        const std::vector<DebugLine>& lines() const { return m_lines; }

        /// The pulses' clock as of the last collect().
        double seconds() const { return m_seconds; }

        /// Segments dropped past MAX_LINES since this was made.
        uint64_t dropped() const { return m_dropped; }

        /// A DebugDraw that draws nothing, for a host that shows none and for a shipped game.
        static DebugDraw& inert();

    private:
        enum class Lifetime : uint8_t { Frame, Tick, Timed };

        struct Pending {
            DebugLine     line;
            float         seconds  = 0.0f;  ///< Timed: what it has left
            uint32_t      tick     = 0;     ///< Tick: the one it was drawn in
            Lifetime      lifetime = Lifetime::Frame;
            DebugCategory category = DebugCategory::General;
        };

        struct Buffer {
            std::vector<Pending> pending;
            uint64_t             dropped = 0;
            uint32_t             tick    = 0;  ///< The tick its Tick lines were last drawn in
            std::thread::id      thread;
        };

    private:
        /// This thread's buffer.
        Buffer& local();

        bool drawing(const DebugStyle& style) const;

        /// A segment drawn in @p style, its ends still to set.
        DebugLine prototype(const DebugStyle& style) const;

        /// Queue @p count segments drawn in @p style on this thread.
        void submit(const DebugLine* lines, size_t count, const DebugStyle& style);
        void submit(const std::vector<DebugLine>& lines, const DebugStyle& style) {
            submit(lines.data(), lines.size(), style);
        }

    private:
        const uint64_t m_id;  ///< What a thread's cached buffer is keyed by; never reused
        bool           m_inert = false;

        std::mutex                           m_buffersMutex;
        std::vector<std::unique_ptr<Buffer>> m_buffers;

        std::atomic<uint32_t> m_shown;
        std::atomic<bool>     m_inTick{false};
        std::atomic<bool>     m_muted{false};
        std::atomic<uint32_t> m_tick{0};

        uint32_t               m_collectedTick = 0;
        uint64_t               m_epoch         = 0;
        double                 m_seconds       = 0.0;
        std::vector<Pending>   m_timed;       ///< Standing for seconds, oldest first
        std::vector<Pending>   m_tickLines;   ///< The last tick's, standing until the next's
        std::vector<DebugLine> m_frameLines;  ///< This frame's, as collected
        std::vector<DebugLine> m_lines;
        bool                   m_collected = false;
        bool                   m_warned    = false;
        uint64_t               m_dropped   = 0;

        DebugFocus m_focus;
        float      m_pointScale = 1.0f;
};

} // namespace Vkm::Engine
