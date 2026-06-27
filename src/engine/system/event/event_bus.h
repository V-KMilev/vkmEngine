#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "l_assert.h"

namespace Engine {

/**
 * @brief Opaque id for a registered listener, returned by subscribe().
 */
using ListenerId = uint32_t;

/**
 * @brief Interface for event bus.
 */
struct IBus {
    virtual ~IBus() = default;
    virtual void flush() = 0;   ///< Drain this bus's queued events to its listeners.
};

/**
 * @brief Listener list plus deferred-event queue for a single event type.
 *
 * Implementation detail of EventSystem: created lazily per event type and
 * driven only through it. The flush-depth guard enforces EventSystem's "no
 * (un)subscribe from inside a listener callback during emit/flush" contract.
 */
template<typename EventT>
class Bus : public IBus {
    public:
        /**
         * @brief Append a listener and return its new id.
         */
        ListenerId subscribe(std::function<void(const EventT&)> cb) {
            const ListenerId id = m_nextId++;
            m_listeners.push_back({id, std::move(cb)});
            return id;
        }

        /**
         * @brief Erase the listener with @p id.
         * @return true if it was found.
         */
        bool remove(ListenerId id) {
            // Mid-flush unsubscribe would invalidate the iteration. The
            // snapshot dance is paid every frame on hot buses; banning it
            // lets emit/flush walk m_listeners by index without copying.
            // Listeners that need self-unsubscribe should enqueue an event
            // to be processed after the current flush returns.
            VKM_ASSERT(m_flushDepth == 0,
                "EventSystem: unsubscribe is not allowed from inside a "
                "listener callback during emit/flush");
            for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
                if (it->id == id) {
                    m_listeners.erase(it);
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Dispatch @p event to every current listener synchronously.
         */
        void emit(const EventT& event) {
            // Subscribe is allowed (push_back stays valid by index past the
            // end of the current loop bound). Unsubscribe is banned above.
            ++m_flushDepth;
            const size_t n = m_listeners.size();
            for (size_t i = 0; i < n; ++i) m_listeners[i].cb(event);
            --m_flushDepth;
        }

        /**
         * @brief Buffer @p event for delivery on the next flush().
         */
        void enqueue(EventT event) {
            m_queue.push_back(std::move(event));
        }

        /**
         * @brief Deliver all queued events to listeners, then clear the queue.
         */
        void flush() override {
            if (m_queue.empty()) return;
            // Swap the queue so re-entrant enqueues land in fresh storage
            // and fire next frame. Listeners are iterated by index against
            // the current bound; subscribe mid-flush is safe (new listeners
            // join next frame's flush), unsubscribe is asserted against.
            std::vector<EventT> localEvents;
            localEvents.swap(m_queue);
            ++m_flushDepth;
            const size_t n = m_listeners.size();
            for (auto& e : localEvents) {
                for (size_t i = 0; i < n; ++i) m_listeners[i].cb(e);
            }
            --m_flushDepth;
        }

    private:
        /**
         * @brief One registered listener: its id and callback.
         */
        struct Entry {
            ListenerId id;
            std::function<void(const EventT&)> cb;
        };

        std::vector<Entry>  m_listeners;   ///< Active listeners, walked by index during dispatch.
        std::vector<EventT> m_queue;       ///< Events awaiting the next flush().
        ListenerId m_nextId     = 1;       ///< Next listener id to hand out.
        int        m_flushDepth = 0;       ///< >0 while inside emit/flush.
};

} // namespace Engine
