#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "l_assert.h"

#include "core/memory/types.h"
#include "core/system.h"

namespace Engine {

/**
 * @brief Typed pub/sub event dispatcher.
 *
 * Subscribe a typed callback for events of type EventT; emit() fires listeners
 * synchronously, enqueue() buffers events flushed once per frame via update().
 *
 * Per-type listener and queue storage is created lazily on first use.
 *
 * Threading: main-thread only. emit / enqueue / subscribe / unsubscribe must
 * all happen on the system's update thread. If a future subsystem (e.g.
 * physics on a worker) needs to push events, add a mutex to Bus<EventT> at
 * that point.
 *
 * Caveats:
 *  - Don't subscribe or unsubscribe from inside a listener callback during
 *    emit/flush - iterates the listener vector and a concurrent mutation
 *    would invalidate it.
 *  - A listener that enqueues an event whose bus has already been flushed
 *    this frame will see that event fire on the next frame's flush.
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

        EventSystem(EventSystem && other) = delete;
        EventSystem& operator=(EventSystem && other) = delete;

        using ListenerId = uint32_t;

        /**
         * @brief Register a callback for events of type EventT.
         * @return ListenerId for later unsubscribe.
         */
        template<typename EventT>
        ListenerId subscribe(std::function<void(const EventT&)> callback) {
            return bus<EventT>().subscribe(std::move(callback));
        }

        /**
         * @brief Remove a previously-registered listener.
         * @return true if found and removed, false otherwise.
         */
        template<typename EventT>
        bool unsubscribe(ListenerId id) {
            auto* b = findBus<EventT>();
            return b ? b->remove(id) : false;
        }

        /**
         * @brief Fire the event synchronously to every listener.
         *
         * Listeners are invoked immediately on the calling thread; no queuing occurs.
         *
         * @tparam EventT The event type whose bus receives the dispatch.
         * @param event The event instance delivered to each subscribed callback.
         */
        template<typename EventT>
        void emit(const EventT& event) {
            auto* b = findBus<EventT>();
            if (b) b->emit(event);
        }

        /**
         * @brief Queue the event for delivery in the next update() flush.
         *
         * Deferred delivery lets producers fire events without recursing into
         * listeners mid-frame; the queued event is dispatched when update() runs.
         *
         * @tparam EventT The event type whose bus receives the queued instance.
         * @param event The event instance to enqueue (moved into the bus queue).
         */
        template<typename EventT>
        void enqueue(EventT event) {
            bus<EventT>().enqueue(std::move(event));
        }

        /**
         * @brief Flush all enqueued events on every bus.
         *
         * Drains every per-type queue, delivering deferred events to their
         * listeners. Called once per frame as the system's update step.
         *
         * @param ctx The shared FrameContext for this frame.
         */
        void update(FrameContext& ctx) override;

    private:
        struct IBus {
            virtual ~IBus() = default;
            virtual void flush() = 0;
            virtual bool remove(ListenerId id) = 0;
        };

        template<typename EventT>
        struct Bus : IBus {
            struct Entry { ListenerId id; std::function<void(const EventT&)> cb; };
            std::vector<Entry> listeners;
            std::vector<EventT> queue;
            ListenerId nextId = 1;
            int        flushDepth = 0;   ///< >0 while inside emit/flush

            ListenerId subscribe(std::function<void(const EventT&)> cb) {
                const ListenerId id = nextId++;
                listeners.push_back({id, std::move(cb)});
                return id;
            }

            bool remove(ListenerId id) override {
                // Mid-flush unsubscribe would invalidate the iteration. The
                // snapshot dance is paid every frame on hot buses; banning
                // it lets emit/flush walk `listeners` by index without
                // copying. Listeners that need self-unsubscribe should
                // enqueue an event to be processed after the current flush
                // returns.
                VKM_ASSERT(flushDepth == 0,
                    "EventSystem: unsubscribe is not allowed from inside a "
                    "listener callback during emit/flush");
                for (auto it = listeners.begin(); it != listeners.end(); ++it) {
                    if (it->id == id) {
                        listeners.erase(it);
                        return true;
                    }
                }
                return false;
            }

            void emit(const EventT& event) {
                // Subscribe is allowed (push_back stays valid by index past
                // the end of the current loop bound). Unsubscribe is banned
                // by the assert above.
                ++flushDepth;
                const size_t n = listeners.size();
                for (size_t i = 0; i < n; ++i) listeners[i].cb(event);
                --flushDepth;
            }

            void enqueue(EventT event) {
                queue.push_back(std::move(event));
            }

            void flush() override {
                if (queue.empty()) return;
                // Swap the queue so re-entrant enqueues land in fresh
                // storage and fire next frame. Listeners are iterated by
                // index against the current bound; subscribe mid-flush is
                // safe (new listeners join next frame's flush), unsubscribe
                // is asserted-against above.
                std::vector<EventT> localEvents;
                localEvents.swap(queue);
                ++flushDepth;
                const size_t n = listeners.size();
                for (auto& e : localEvents) {
                    for (size_t i = 0; i < n; ++i) listeners[i].cb(e);
                }
                --flushDepth;
            }
        };

        template<typename EventT>
        Bus<EventT>& bus() {
            const TypeId id = typeId<EventT>();
            if (id >= m_buses.size()) m_buses.resize(id + 1);
            if (!m_buses[id]) m_buses[id] = std::make_unique<Bus<EventT>>();
            return *static_cast<Bus<EventT>*>(m_buses[id].get());
        }

        template<typename EventT>
        Bus<EventT>* findBus() {
            const TypeId id = typeId<EventT>();
            if (id >= m_buses.size() || !m_buses[id]) return nullptr;
            return static_cast<Bus<EventT>*>(m_buses[id].get());
        }

        std::vector<std::unique_ptr<IBus>> m_buses;
};

} // namespace Engine
