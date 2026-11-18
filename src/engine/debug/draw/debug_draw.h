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

#include "debug/draw/debug_palette.h"
#include "debug/draw/debug_category.h"
#include "ecs/entity.h"
#include "debug/draw/debug_line.h"

namespace Vkm::Engine {

/**
 * @brief How a drawing looks: color, width, lifetime, depth and category.
 *
 * A color converts to one, so `debugDraw().sphere(center, radius, color)` is a whole call; the
 * rest chains: `DebugStyle(color).wide(3.0f).lasting(2.0f)`.
 */
struct DebugStyle {
    glm::vec4     color    = glm::vec4(0.0f);  ///< Display RGBA; zero alpha takes the category's
    float         width    = 1.5f;             ///< Points: pixels at a density of 1
    float         seconds  = 0.0f;             ///< Of the game's time it stands; 0, see DebugDraw
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
};

/**
 * @brief Who DebugViewSystem centers on - the editor's selection: drawn in the highlight and,
 *        while detailForFocusOnly, alone in drawing their detail.
 */
struct DebugFocus {
    std::vector<EntityId> entities;  ///< Sorted by DebugDraw::setFocus
    EntityId              primary{};

    bool detailForFocusOnly = false;  ///< A light's reach, a sound's spheres, a volume's probes, an emitter's
                                      ///< shape, a rig's axes: the focus's alone, or a scene of lamps buries
                                      ///< itself in wire.

    bool has(EntityId id) const;
};

/**
 * @brief Immediate-mode drawing in the world: lines and shapes that anything submits
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
 * Reached as FrameContext::debug and BehaviorBase::debugDraw(). A category turned off returns at its
 * first check, before any segment is built. Its settings - the categories shown, the focus - are its
 * own, not a host's product: the editor sets them as a game would, and DebugViewSystem and
 * RenderSystem read them off it alone.
 */
class DebugDraw {
    public:
        static constexpr uint32_t MAX_LINES = 1u << 16;  ///< The most segments a frame draws.

        static constexpr int CIRCLE_SEGMENTS = 32;  ///< Straight pieces in a full circle.

        static constexpr float POINT_WIDTHS = 4.0f;  ///< A point is a dot this many of its style's widths
                                                     ///< across.

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

        /// The circle round @p center facing @p normal.
        void circle(
            const glm::vec3& center,
            const glm::vec3& normal,
            float radius,
            const DebugStyle& style = {}
        );

        /// center + radius (cos t axisA + sin t axisB) round a full turn: an ellipse for unequal axes.
        void circle(
            const glm::vec3& center,
            const glm::vec3& axisA,
            const glm::vec3& axisB,
            float radius,
            const DebugStyle& style = {}
        );

        /// The same from angle @p from to @p to, radians.
        void arc(
            const glm::vec3& center,
            const glm::vec3& axisA,
            const glm::vec3& axisB,
            float radius,
            float from,
            float to,
            const DebugStyle& style = {}
        );

        /// Three great circles, one on each world plane.
        void sphere(const glm::vec3& center, float radius, const DebugStyle& style = {});

        /// The box of @p halfExtents round @p center, turned by @p rotation.
        void box(
            const glm::vec3& center,
            const glm::vec3& halfExtents,
            const glm::quat& rotation,
            const DebugStyle& style = {}
        );
        void box(const glm::vec3& center, const glm::vec3& halfExtents, const DebugStyle& style = {}) {
            box(center, halfExtents, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), style);
        }
        /// A color spelled out, as glm would take an RGB one for a rotation and an RGBA one for RGB.
        void box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& color) {
            box(center, halfExtents, DebugStyle(color));
        }
        void box(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color) {
            box(center, halfExtents, DebugStyle(color));
        }

        /// The world-aligned box from @p min to @p max.
        void bounds(const glm::vec3& min, const glm::vec3& max, const DebugStyle& style = {}) {
            box((min + max) * 0.5f, (max - min) * 0.5f, style);
        }

        /// The capsule @p radius round the segment from @p a to @p b.
        void capsule(const glm::vec3& a, const glm::vec3& b, float radius, const DebugStyle& style = {});

        /// The same round @p center along @p rotation's +Y, so its wires turn with the rotation.
        void capsule(
            const glm::vec3& center,
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

        /// @p frame's three axes from its origin, @p length long, in DebugPalette::AXES.
        void axes(const glm::mat4& frame, float length, const DebugStyle& style = {});

        // What is shown: the host's - the editor's View > Show, or a game run without it.
        void setShown(DebugCategory category, bool shown);
        bool shown(DebugCategory category) const;

        /// The focus DebugViewSystem centers on; its entities are sorted here.
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
         * @param simSeconds The game's seconds since the last frame, which the timed ones age by.
         * @param worldEpoch Scene::epoch(): a world replaced takes what was drawn in it along,
         *                   its last frame's ticks and seconds included.
         */
        void collect(float simSeconds, uint64_t worldEpoch);

        /// The frame ends: what it drew for itself goes if nothing collected it - nothing showed it.
        void endFrame();

        /// The frame's segments, after collect().
        const std::vector<DebugLine>& lines() const { return m_lines; }


        /// Segments dropped past MAX_LINES since this was made.
        uint64_t dropped() const { return m_dropped; }

        /// A DebugDraw that draws nothing, for a host that shows none and for a shipped game.
        static DebugDraw& inert();

    private:
        enum class Lifetime : uint8_t { Frame, Tick, Timed };

        struct Inert {};

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
        /// One that draws nothing: inert()'s.
        explicit DebugDraw(Inert);

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
        std::vector<Pending>   m_timed;      ///< Standing for seconds, oldest first
        std::vector<Pending>   m_tickLines;  ///< The last tick's, standing until the next's
        std::vector<DebugLine> m_lines;
        bool                   m_collected     = false;
        bool                   m_warned        = false;
        uint64_t               m_dropped       = 0;

        DebugFocus m_focus;
        float      m_pointScale = 1.0f;
};

} // namespace Vkm::Engine
