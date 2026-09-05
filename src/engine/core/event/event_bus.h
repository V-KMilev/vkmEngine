#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/memory/type_registry.h"
#include "core/memory/types.h"
#include "core/event/bus.h"

namespace Vkm::Engine {

/**
 * @brief Typed pub/sub event dispatcher.
 *
 * Engine infrastructure, not a System: the Engine owns one by value, like the
 * Clock, carries it on every FrameContext, and calls flush() at the top of the
 * Simulation stage - the fixed, visible point where queued events deliver. That
 * point is reached once per fixed tick and once more per frame for whatever was
 * queued outside a tick; flush drains, so the second never repeats the first.
 *
 * Per-type listener and queue storage is created lazily on first use.
 *
 * Main-thread only: emit, enqueue, subscribe and unsubscribe all happen on the
 * frame thread, and a subsystem that wants to push events from a worker is the
 * point at which Bus<EventT> would need a mutex.
 *
 * Subscribing and unsubscribing from inside a callback are both allowed - a
 * one-shot listener retiring itself is the case that shaped Bus::remove.
 * docs/reference/system/events.md has the delivery rules and the caveats.
 */
class EventBus {
    public:
        EventBus() = default;
        ~EventBus() = default;

        EventBus(const EventBus& other) = delete;
        EventBus& operator=(const EventBus& other) = delete;

        EventBus(EventBus && other) noexcept = delete;
        EventBus& operator=(EventBus && other) noexcept = delete;

    public:
        /**
         * @brief Register a callback for events of type EventT.
         * @return ListenerId for a later unsubscribe().
         */
        template<typename EventT>
        ListenerId subscribe(std::function<void(const EventT&)> callback) {
            return bus<EventT>().subscribe(std::move(callback));
        }

        /**
         * @brief Remove a previously-registered listener.
         * @return true if it was found and removed.
         */
        template<typename EventT>
        bool unsubscribe(ListenerId id) {
            auto* b = findBus<EventT>();
            return b ? b->remove(id) : false;
        }

        /**
         * @brief Fire @p event synchronously to every listener now, on the calling thread.
         */
        template<typename EventT>
        void emit(const EventT& event) {
            if (auto* b = findBus<EventT>()) b->emit(event);
        }

        /**
         * @brief Queue @p event for delivery on the next flush() (no mid-frame recursion).
         */
        template<typename EventT>
        void enqueue(EventT event) {
            bus<EventT>().enqueue(std::move(event));
        }

        /**
         * @brief Drain every per-type queue to its listeners.
         *
         * Called by Engine::run at the top of the Simulation stage, before any
         * gameplay system ticks: once per fixed tick, and once more per frame
         * for what was queued outside a tick or by the frame's last one.
         * Delivering on the frame alone would put a reaction however many ticks
         * later the frame rate decided.
         */
        void flush();

    private:
        /**
         * @brief Return the bus for EventT, creating it on first use.
         */
        template<typename EventT>
        Bus<EventT>& bus() {
            return static_cast<Bus<EventT>&>(m_buses.ensure<EventT>(
                [] { return std::make_unique<Bus<EventT>>(); }));
        }

        /**
         * @brief Return the bus for EventT, or nullptr if none exists yet.
         */
        template<typename EventT>
        Bus<EventT>* findBus() {
            return static_cast<Bus<EventT>*>(m_buses.find<EventT>());
        }

    private:
        TypeRegistry<IBus> m_buses;
};

} // namespace Vkm::Engine
