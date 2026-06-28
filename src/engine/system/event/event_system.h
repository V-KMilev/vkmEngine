#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/memory/types.h"
#include "core/system.h"
#include "system/event/event_bus.h"

namespace Engine {

/**
 * @brief Typed pub/sub event dispatcher.
 *
 * Subscribe a typed callback for events of type EventT; emit() fires listeners
 * synchronously, enqueue() buffers events flushed once per frame via update().
 * Per-type listener and queue storage is created lazily on first use.
 *
 * Threading: main-thread only. emit / enqueue / subscribe / unsubscribe must
 * all happen on the system's update thread. If a future subsystem (e.g. physics
 * on a worker) needs to push events, add a mutex to Bus<EventT> at that point.
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
 *   events.enqueue(DamageEvent{target, 25});    // deferred until next update()
 *   events.unsubscribe<DamageEvent>(id);
 */
class EventSystem : public System {
    public:
        EventSystem() = default;
        ~EventSystem() override = default;

        EventSystem(const EventSystem& other) = delete;
        EventSystem& operator=(const EventSystem& other) = delete;

        EventSystem(EventSystem && other) noexcept = delete;
        EventSystem& operator=(EventSystem && other) noexcept = delete;

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
         * @brief Queue @p event for delivery on the next update() flush (no mid-frame recursion).
         */
        template<typename EventT>
        void enqueue(EventT event) {
            bus<EventT>().enqueue(std::move(event));
        }

        /**
         * @brief Drain every per-type queue to its listeners. Runs once per frame as the update step.
         */
        void update(FrameContext& ctx) override;

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

} // namespace Engine
