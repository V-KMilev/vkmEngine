#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/memory/types.h"
#include "core/event/bus.h"

namespace Vkm::Engine {

/**
 * @brief Typed pub/sub event dispatcher.
 *
 * Engine infrastructure, not a System: the Engine owns one by value (like the
 * Clock and WindowManager), carries it on every FrameContext, and calls
 * flush() at the top of the Simulation stage - the fixed, visible point where
 * queued events deliver.
 *
 * Per-type listener and queue storage is created lazily on first use.
 *
 * Threading: main-thread only. emit / enqueue / subscribe / unsubscribe must
 * all happen on the frame thread. If a future subsystem (e.g. physics on a
 * worker) needs to push events, add a mutex to Bus<EventT> at that point.
 *
 * Caveats:
 *  - Don't subscribe or unsubscribe from inside a listener callback during
 *    emit/flush - it iterates the listener vector and a concurrent mutation
 *    would invalidate it (unsubscribe is asserted against; see Bus::remove).
 *  - A listener that enqueues an event whose bus has already been flushed this
 *    frame will see that event fire on the next frame's flush.
 *
 * Usage:
 *   struct DamageEvent { EntityId target; int amount; };
 *   auto id = events.subscribe<DamageEvent>([](const DamageEvent& e) { ... });
 *   events.emit(DamageEvent{target, 50});       // sync
 *   events.enqueue(DamageEvent{target, 25});    // deferred until next flush()
 *   events.unsubscribe<DamageEvent>(id);
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
         * Called once per frame by Engine::run at the top of the Simulation
         * stage, before any gameplay system ticks.
         */
        void flush();

    private:
        /**
         * @brief Return the bus for EventT, creating it on first use.
         */
        template<typename EventT>
        Bus<EventT>& bus() {
            const TypeId id = typeId<EventT>();
            if (id >= m_buses.size()) m_buses.resize(id + 1);
            if (!m_buses[id]) m_buses[id] = std::make_unique<Bus<EventT>>();
            return *static_cast<Bus<EventT>*>(m_buses[id].get());
        }

        /**
         * @brief Return the bus for EventT, or nullptr if none exists yet.
         */
        template<typename EventT>
        Bus<EventT>* findBus() {
            const TypeId id = typeId<EventT>();
            if (id >= m_buses.size() || !m_buses[id]) return nullptr;
            return static_cast<Bus<EventT>*>(m_buses[id].get());
        }

    private:
        std::vector<std::unique_ptr<IBus>> m_buses;
};

} // namespace Vkm::Engine
