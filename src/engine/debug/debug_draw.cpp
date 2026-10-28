#define VKM_LOG_CATEGORY "DEBUG"

#include "debug/debug_draw.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include "logger.h"

#include "core/math/axes.h"
#include "core/math/rotation.h"

namespace Vkm::Engine {

namespace {

/// Hands each DebugDraw an id no other has had, so a thread's cached buffer never answers for a new one.
std::atomic<uint64_t> g_nextId{1};

/// This thread's buffer in the DebugDraw it last drew into.
struct LocalBuffer {
    uint64_t owner  = 0;
    void*    buffer = nullptr;
};
thread_local LocalBuffer t_local;

/// Where a primitive lays its segments out before they are queued.
thread_local std::vector<DebugLine> t_scratch;

/// How far a thread's queue may run past MAX_LINES before submit() drops its oldest, so the drop is
/// paid once a quarter of the cap, not once a segment.
constexpr size_t QUEUE_SLACK = DebugDraw::MAX_LINES / 4;

static_assert(static_cast<uint32_t>(DebugCategory::Count) <= 32, "DebugDraw's shown mask is 32 bits");

/// What shows until a host sets otherwise: gameplay's own lines (General).
constexpr uint32_t SHOWN_AT_START = 1u << static_cast<uint32_t>(DebugCategory::General);

/// @p proto from @p a to @p b, @p arcA and @p arcB along its path.
DebugLine between(
    const DebugLine& proto,
    const glm::vec3& a,
    const glm::vec3& b,
    float arcA = 0.0f,
    float arcB = 0.0f
) {
    DebugLine line = proto;
    line.a = glm::vec4(a, arcA);
    line.b = glm::vec4(b, arcB);
    return line;
}

/// The arc centre + radius (cos t axisA + sin t axisB), t from @p from to @p to, onto @p out.
void appendArc(
    std::vector<DebugLine>& out,
    const DebugLine& proto,
    const glm::vec3& centre,
    const glm::vec3& axisA,
    const glm::vec3& axisB,
    float radius,
    float from,
    float to
) {
    const float turn     = std::abs(to - from) / glm::two_pi<float>();
    const int   segments = std::max(2, static_cast<int>(std::ceil(turn * DebugDraw::CIRCLE_SEGMENTS)));
    glm::vec3   last     = centre + (axisA * std::cos(from) + axisB * std::sin(from)) * radius;
    for (int s = 1; s <= segments; ++s) {
        const float     t    = from + (to - from) * (static_cast<float>(s) / static_cast<float>(segments));
        const glm::vec3 next = centre + (axisA * std::cos(t) + axisB * std::sin(t)) * radius;
        out.push_back(between(proto, last, next));
        last = next;
    }
}

/// The capsule round the segment from @p bottom to @p top, its rings spanned by @p u and @p v.
void appendCapsule(
    std::vector<DebugLine>& out,
    const DebugLine& proto,
    const glm::vec3& bottom,
    const glm::vec3& top,
    const glm::vec3& u,
    const glm::vec3& v,
    float radius
) {
    const glm::vec3 axis = glm::normalize(top - bottom);
    appendArc(out, proto, top, u, v, radius, 0.0f, glm::two_pi<float>());
    appendArc(out, proto, bottom, u, v, radius, 0.0f, glm::two_pi<float>());
    for (const glm::vec3& side : {u, -u, v, -v}) {
        out.push_back(between(proto, bottom + side * radius, top + side * radius));
    }
    appendArc(out, proto, top, u, axis, radius, 0.0f, glm::pi<float>());
    appendArc(out, proto, top, v, axis, radius, 0.0f, glm::pi<float>());
    appendArc(out, proto, bottom, u, -axis, radius, 0.0f, glm::pi<float>());
    appendArc(out, proto, bottom, v, -axis, radius, 0.0f, glm::pi<float>());
}

/// The twelve edges between @p corners, numbered x in bit 2, y in bit 1, z in bit 0.
void appendCorners(std::vector<DebugLine>& out, const DebugLine& proto, const glm::vec3 (&corners)[8]) {
    // An edge joins two corners one bit apart.
    static constexpr int EDGES[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},
        {0, 2}, {1, 3}, {4, 6}, {5, 7},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (const auto& edge : EDGES) out.push_back(between(proto, corners[edge[0]], corners[edge[1]]));
}

/// RGBA8, red in the low byte, as DebugLine::color holds it.
uint32_t pack(const glm::vec4& color) {
    return glm::packUnorm4x8(glm::clamp(color, 0.0f, 1.0f));
}

bool entityBefore(EntityId a, EntityId b) {
    return a.slot() != b.slot() ? a.slot() < b.slot() : a.generation() < b.generation();
}

} // namespace

bool DebugFocus::has(EntityId id) const {
    return std::binary_search(entities.begin(), entities.end(), id, entityBefore);
}

DebugDraw::DebugDraw()
    : m_id(g_nextId.fetch_add(1, std::memory_order_relaxed))
    , m_shown(SHOWN_AT_START) {}

DebugDraw::~DebugDraw() = default;

DebugDraw& DebugDraw::inert() {
    static DebugDraw* const s_none = [] {
        static DebugDraw s_draw;
        s_draw.m_inert = true;
        return &s_draw;
    }();
    return *s_none;
}

DebugDraw::Buffer& DebugDraw::local() {
    if (t_local.owner == m_id) return *static_cast<Buffer*>(t_local.buffer);

    const std::thread::id             self = std::this_thread::get_id();
    const std::lock_guard<std::mutex> lock(m_buffersMutex);
    Buffer* found = nullptr;
    for (const std::unique_ptr<Buffer>& buffer : m_buffers) {
        if (buffer->thread == self) found = buffer.get();
    }
    if (!found) {
        m_buffers.push_back(std::make_unique<Buffer>());
        found         = m_buffers.back().get();
        found->thread = self;
    }
    t_local = {m_id, found};
    return *found;
}

bool DebugDraw::drawing(const DebugStyle& style) const {
    if (m_inert || m_muted.load(std::memory_order_relaxed)) return false;
    return (m_shown.load(std::memory_order_relaxed) >> static_cast<uint32_t>(style.category)) & 1u;
}

DebugLine DebugDraw::prototype(const DebugStyle& style) const {
    DebugLine line;
    line.color = pack(style.color.a > 0.0f ? style.color : DebugPalette::of(style.category).value());
    line.width = style.width;
    line.glow  = style.glow;
    line.flags = static_cast<uint32_t>(style.depth);
    return line;
}

void DebugDraw::submit(const DebugLine* lines, size_t count, const DebugStyle& style) {
    if (count == 0) return;
    const Lifetime lifetime = style.seconds > 0.0f ? Lifetime::Timed
        : m_inTick.load(std::memory_order_relaxed) ? Lifetime::Tick
        : Lifetime::Frame;
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);

    Buffer& buffer = local();
    std::vector<Pending>& pending = buffer.pending;
    // A later tick's lines leave an earlier one's still queued with nothing to show - collect()
    // keeps the last tick's - so they go now, or a frame catching up on ticks fills the queue.
    if (lifetime == Lifetime::Tick && buffer.tick != tick) {
        const auto stale = [tick](const Pending& p) {
            return p.lifetime == Lifetime::Tick && p.tick != tick;
        };
        pending.erase(std::remove_if(pending.begin(), pending.end(), stale), pending.end());
        buffer.tick = tick;
    }
    for (size_t i = 0; i < count; ++i) {
        pending.push_back({lines[i], style.seconds, tick, lifetime, style.category});
    }

    // Bounded before a collect too: a runaway loop loses its oldest rather than all memory.
    if (pending.size() > MAX_LINES + QUEUE_SLACK) {
        const size_t excess = pending.size() - MAX_LINES;
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(excess));
        buffer.dropped += excess;
    }
}

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style) {
    if (!drawing(style)) return;
    const DebugLine one = between(prototype(style), a, b);
    submit(&one, 1, style);
}

void DebugDraw::polyline(const glm::vec3* points, size_t count, const DebugStyle& style, bool closed) {
    if (!drawing(style) || count < 2) return;
    const DebugLine proto = prototype(style);
    t_scratch.clear();
    for (size_t i = 0; i + 1 < count; ++i) t_scratch.push_back(between(proto, points[i], points[i + 1]));
    if (closed && count > 2) t_scratch.push_back(between(proto, points[count - 1], points[0]));
    submit(t_scratch, style);
}

void DebugDraw::point(const glm::vec3& at, const DebugStyle& style) {
    if (!drawing(style)) return;
    DebugLine dot = between(prototype(style), at, at);
    dot.width *= POINT_WIDTHS;
    submit(&dot, 1, style);
}

void DebugDraw::arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style) {
    if (!drawing(style)) return;
    const DebugLine proto = prototype(style);
    t_scratch.clear();
    t_scratch.push_back(between(proto, from, to));

    const glm::vec3 shaft  = to - from;
    const float     length = glm::length(shaft);
    if (length > 1e-6f) {
        // Four barbs, so the head reads as one from any side.
        const glm::vec3 dir  = shaft / length;
        const float     head = length * 0.2f;
        glm::vec3 across, up;
        Math::orthoBasis(dir, across, up);
        const glm::vec3 base = to - dir * head;
        for (const glm::vec3& side : {across, -across, up, -up}) {
            t_scratch.push_back(between(proto, to, base + side * (head * 0.4f)));
        }
    }
    submit(t_scratch, style);
}

void DebugDraw::circle(
    const glm::vec3& centre,
    const glm::vec3& normal,
    float radius,
    const DebugStyle& style
) {
    if (!drawing(style) || glm::length(normal) <= 1e-6f) return;
    glm::vec3 axisA, axisB;
    Math::orthoBasis(glm::normalize(normal), axisA, axisB);
    circle(centre, axisA, axisB, radius, style);
}

void DebugDraw::circle(
    const glm::vec3& centre,
    const glm::vec3& axisA,
    const glm::vec3& axisB,
    float radius,
    const DebugStyle& style
) {
    arc(centre, axisA, axisB, radius, 0.0f, glm::two_pi<float>(), style);
}

void DebugDraw::arc(
    const glm::vec3& centre,
    const glm::vec3& axisA,
    const glm::vec3& axisB,
    float radius,
    float from,
    float to,
    const DebugStyle& style
) {
    if (!drawing(style)) return;
    t_scratch.clear();
    appendArc(t_scratch, prototype(style), centre, axisA, axisB, radius, from, to);
    submit(t_scratch, style);
}

void DebugDraw::sphere(const glm::vec3& centre, float radius, const DebugStyle& style) {
    if (!drawing(style)) return;
    const DebugLine proto = prototype(style);
    const float     turn  = glm::two_pi<float>();
    t_scratch.clear();
    appendArc(t_scratch, proto, centre, Math::WORLD_AXIS_X, Math::WORLD_AXIS_Y, radius, 0.0f, turn);
    appendArc(t_scratch, proto, centre, Math::WORLD_AXIS_X, Math::WORLD_AXIS_Z, radius, 0.0f, turn);
    appendArc(t_scratch, proto, centre, Math::WORLD_AXIS_Y, Math::WORLD_AXIS_Z, radius, 0.0f, turn);
    submit(t_scratch, style);
}

void DebugDraw::box(
    const glm::vec3& centre,
    const glm::vec3& halfExtents,
    const glm::quat& rotation,
    const DebugStyle& style
) {
    if (!drawing(style)) return;
    const glm::mat3 turn = glm::mat3_cast(rotation);
    glm::vec3 corners[8];
    for (int k = 0; k < 8; ++k) {
        const glm::vec3 sign((k & 4) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 1) ? 1.0f : -1.0f);
        corners[k] = centre + turn * (halfExtents * sign);
    }
    t_scratch.clear();
    appendCorners(t_scratch, prototype(style), corners);
    submit(t_scratch, style);
}

void DebugDraw::capsule(const glm::vec3& a, const glm::vec3& b, float radius, const DebugStyle& style) {
    if (!drawing(style)) return;
    const glm::vec3 axis = b - a;
    if (glm::length(axis) <= 1e-6f) {
        sphere(a, radius, style);
        return;
    }
    glm::vec3 u, v;
    Math::orthoBasis(glm::normalize(axis), u, v);
    t_scratch.clear();
    appendCapsule(t_scratch, prototype(style), a, b, u, v, radius);
    submit(t_scratch, style);
}

void DebugDraw::capsule(
    const glm::vec3& centre,
    const glm::quat& rotation,
    float radius,
    float halfHeight,
    const DebugStyle& style
) {
    if (!drawing(style)) return;
    const glm::mat3 turn = glm::mat3_cast(rotation);
    if (halfHeight <= 1e-6f) {
        sphere(centre, radius, style);
        return;
    }
    t_scratch.clear();
    appendCapsule(
        t_scratch,
        prototype(style),
        centre - turn[1] * halfHeight,
        centre + turn[1] * halfHeight,
        turn[0],
        turn[2],
        radius
    );
    submit(t_scratch, style);
}

void DebugDraw::cone(const glm::vec3& apex, const glm::vec3& axis, float halfAngle, const DebugStyle& style) {
    const float length = glm::length(axis);
    if (!drawing(style) || length <= 1e-6f) return;
    const glm::vec3 dir = axis / length;
    glm::vec3 u, v;
    Math::orthoBasis(dir, u, v);
    // Short of a right angle, past which the cone has no base.
    const float     radius = length * std::tan(std::min(std::abs(halfAngle), glm::radians(89.0f)));
    const glm::vec3 base   = apex + axis;
    const DebugLine proto  = prototype(style);
    t_scratch.clear();
    appendArc(t_scratch, proto, base, u, v, radius, 0.0f, glm::two_pi<float>());
    for (const glm::vec3& side : {u, -u, v, -v}) {
        t_scratch.push_back(between(proto, apex, base + side * radius));
    }
    submit(t_scratch, style);
}

void DebugDraw::frustum(const glm::mat4& invViewProjection, const DebugStyle& style) {
    if (!drawing(style)) return;
    glm::vec3 corners[8];
    for (int k = 0; k < 8; ++k) {
        const glm::vec4 clip((k & 4) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 1) ? 1.0f : -1.0f, 1.0f);
        const glm::vec4 world = invViewProjection * clip;
        corners[k] = glm::vec3(world) / world.w;
    }
    t_scratch.clear();
    appendCorners(t_scratch, prototype(style), corners);
    submit(t_scratch, style);
}

void DebugDraw::axes(const glm::mat4& frame, float length, const DebugStyle& style) {
    if (!drawing(style)) return;
    const glm::vec3 origin(frame[3]);
    DebugLine proto = prototype(style);
    t_scratch.clear();
    for (int axis = 0; axis < 3; ++axis) {
        const glm::vec3 dir(frame[axis]);
        const float     size = glm::length(dir);
        if (size <= 1e-6f) continue;
        const Math::AxisColor c = Math::AXIS_COLORS[axis];
        proto.color = pack(DebugPalette::Color{c.r, c.g, c.b, 255}.value());
        t_scratch.push_back(between(proto, origin, origin + dir * (length / size)));
    }
    submit(t_scratch, style);
}

void DebugDraw::pulse(const glm::vec3* path, size_t count, const DebugStyle& style, const DebugPulse& pulse) {
    if (!drawing(style) || count < 2) return;
    DebugLine proto = prototype(style);
    proto.pulse = glm::vec4(pulse.dash, std::max(pulse.spacing, 1e-3f), pulse.speed, pulse.phase);
    proto.flags |= static_cast<uint32_t>(std::lround(glm::clamp(pulse.trail, 0.0f, 1.0f) * 255.0f)) << 8;
    t_scratch.clear();
    float arc = 0.0f;
    for (size_t i = 0; i + 1 < count; ++i) {
        const float next = arc + glm::distance(path[i], path[i + 1]);
        t_scratch.push_back(between(proto, path[i], path[i + 1], arc, next));
        arc = next;
    }
    submit(t_scratch, style);
}

void DebugDraw::setShown(DebugCategory category, bool shown) {
    const uint32_t bit = 1u << static_cast<uint32_t>(category);
    if (shown) m_shown.fetch_or(bit, std::memory_order_relaxed);
    else       m_shown.fetch_and(~bit, std::memory_order_relaxed);
}

bool DebugDraw::shown(DebugCategory category) const {
    return (m_shown.load(std::memory_order_relaxed) >> static_cast<uint32_t>(category)) & 1u;
}

void DebugDraw::setFocus(DebugFocus focus) {
    std::sort(focus.entities.begin(), focus.entities.end(), entityBefore);
    m_focus = std::move(focus);
}

void DebugDraw::beginTick() {
    m_tick.fetch_add(1, std::memory_order_relaxed);
    m_inTick.store(true, std::memory_order_relaxed);
}

void DebugDraw::collect(float seconds, float simSeconds, uint64_t worldEpoch) {
    m_seconds  += seconds;
    m_collected = true;

    // What stood belonged to the world it was drawn in - and so did what this frame's ticks drew
    // and what was given seconds this frame, before the world was replaced in it.
    const bool replaced = worldEpoch != m_epoch;
    if (replaced) {
        m_epoch = worldEpoch;
        m_timed.clear();
        m_tickLines.clear();
    }

    // Aged before this frame's join them, so each stands its whole first frame.
    for (Pending& timed : m_timed) timed.seconds -= simSeconds;
    const auto spent = [](const Pending& timed) { return timed.seconds <= 0.0f; };
    m_timed.erase(std::remove_if(m_timed.begin(), m_timed.end(), spent), m_timed.end());

    // A tick ran: what it drew - nothing, even - replaces what the last one drew.
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    if (tick != m_collectedTick) {
        m_tickLines.clear();
        m_collectedTick = tick;
    }

    uint64_t dropped = 0;
    m_frameLines.clear();
    {
        const std::lock_guard<std::mutex> buffers(m_buffersMutex);
        for (const std::unique_ptr<Buffer>& buffer : m_buffers) {
            for (const Pending& pending : buffer->pending) {
                switch (pending.lifetime) {
                    case Lifetime::Timed:
                        if (!replaced) m_timed.push_back(pending);
                        break;
                    // An earlier tick of this frame's drew what the last one replaced.
                    case Lifetime::Tick:
                        if (!replaced && pending.tick == tick) m_tickLines.push_back(pending);
                        break;
                    case Lifetime::Frame:
                        m_frameLines.push_back(pending.line);
                        break;
                }
            }
            buffer->pending.clear();
            dropped += std::exchange(buffer->dropped, 0);
        }
    }

    // Past the cap the oldest go: what has stood for seconds, then the tick's, then the frame's.
    const size_t total = m_timed.size() + m_tickLines.size() + m_frameLines.size();
    if (total > MAX_LINES) {
        size_t excess = total - MAX_LINES;
        const auto drop = [&](auto& list) {
            const size_t n = std::min(excess, list.size());
            list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(n));
            excess  -= n;
            dropped += n;
        };
        drop(m_timed);
        drop(m_tickLines);
        drop(m_frameLines);
    }

    // What stands shows while its category does; hidden, it waits to show again rather than goes.
    const uint32_t shownNow = m_shown.load(std::memory_order_relaxed);
    const auto     showing  = [shownNow](const Pending& p) {
        return (shownNow >> static_cast<uint32_t>(p.category)) & 1u;
    };
    m_lines.clear();
    m_lines.reserve(m_timed.size() + m_tickLines.size() + m_frameLines.size());
    for (const Pending& timed : m_timed) {
        if (showing(timed)) m_lines.push_back(timed.line);
    }
    for (const Pending& ticked : m_tickLines) {
        if (showing(ticked)) m_lines.push_back(ticked.line);
    }
    m_lines.insert(m_lines.end(), m_frameLines.begin(), m_frameLines.end());

    if (dropped == 0) return;
    m_dropped += dropped;
    if (!m_warned) {
        m_warned = true;
        LOG_WARNING("More than %u debug lines in a frame: the oldest are dropped", MAX_LINES);
    }
}

void DebugDraw::endFrame() {
    if (!m_collected) {
        // Nothing showed this frame: what it drew for itself goes, and a superseded tick's with it;
        // what stands for a tick or for seconds waits for a frame that shows.
        const uint32_t tick = m_tick.load(std::memory_order_relaxed);
        const std::lock_guard<std::mutex> buffers(m_buffersMutex);
        for (const std::unique_ptr<Buffer>& buffer : m_buffers) {
            std::vector<Pending>& pending = buffer->pending;
            pending.erase(
                std::remove_if(pending.begin(), pending.end(), [tick](const Pending& p) {
                    return p.lifetime == Lifetime::Frame || (p.lifetime == Lifetime::Tick && p.tick != tick);
                }),
                pending.end()
            );
        }
    }
    m_collected = false;
}

} // namespace Vkm::Engine
