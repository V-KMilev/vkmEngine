#pragma once

#include <cstdint>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "l_assert.h"

namespace Vkm::Engine {

/**
 * @brief Opaque id for a registered listener, returned by subscribe().
 */
using ListenerId = uint32_t;

struct IBus {
    virtual ~IBus() = default;

    /**
     * @brief Drain this bus's queued events to its listeners.
     */
    virtual void flush() = 0;
};

/**
 * @brief Listener list plus deferred-event queue for a single event type.
 *
 * Implementation detail of EventBus: created lazily per event type and
 * driven only through it. The flush-depth guard enforces EventBus's "no
 * (un)subscribe from inside a listener callback during emit/flush" contract.
 */
template<typename EventT>
class Bus : public IBus {
    public:
        Bus() = default;
        ~Bus() override = default;

        Bus(const Bus& other) = delete;
        Bus& operator=(const Bus& other) = delete;

        Bus(Bus && other) = delete;
        Bus& operator=(Bus && other) = delete;

    public:
        /**
         * @brief Append a listener and return its new id.
         *
         * Subscribing from inside a listener is legitimate - a spawned object
         * registering itself - but the entry only joins the live list once the
         * outermost dispatch has unwound. Appending to a list a dispatch is
         * walking can reallocate it, which moves-then-destroys the
         * std::function whose operator() is on the stack at that moment.
         */
        ListenerId subscribe(std::function<void(const EventT&)> cb) {
            const ListenerId id = m_nextId++;

            if (m_flushDepth > 0) m_pending.push_back({id, std::move(cb)});
            else                  m_listeners.push_back({id, std::move(cb)});
            return id;
        }

        /**
         * @brief Erase the listener with @p id.
         *
         * Callable from inside a listener, on itself included: mid-dispatch the
         * entry is emptied rather than erased, because emit and flush walk by
         * index and an erase under them would move every later listener down one
         * and skip whichever took the freed slot. The empty slot is skipped by
         * the walk and reaped by admitPending once the outermost dispatch
         * unwinds. One that subscribed during this same dispatch is waiting in
         * m_pending instead, and comes straight back out of it - nothing walks
         * that list until the dispatch has unwound, so it can be erased outright.
         *
         * @return true if it was found and erased.
         */
        bool remove(ListenerId id) {
            for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
                if (it->id != id) continue;

                if (m_flushDepth != 0) it->cb = nullptr;
                else                   m_listeners.erase(it);
                return true;
            }
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                if (it->id != id) continue;

                m_pending.erase(it);
                return true;
            }
            return false;
        }

        /**
         * @brief Dispatch @p event to every current listener synchronously.
         */
        void emit(const EventT& event) {
            ++m_flushDepth;
            const size_t n = m_listeners.size();
            for (size_t i = 0; i < n; ++i) {
                if (m_listeners[i].cb) m_listeners[i].cb(event);
            }
            --m_flushDepth;
            admitPending();
        }

        /**
         * @brief Buffer @p event for delivery on the next flush().
         */
        void enqueue(EventT event) {
            m_queue.push_back(std::move(event));
        }

        /**
         * @brief Deliver all queued events to listeners, then clear the queue.
         *
         * The queue is swapped aside before the walk, so an event a listener
         * enqueues lands in fresh storage and fires next frame instead of
         * extending the batch being delivered. It swaps with a retained member
         * rather than a local because a local would take the queue's buffer and
         * free it at scope exit, so every flush on a hot bus paid an allocation
         * to rebuild what it had just thrown away. Two buffers ping-pong, and a
         * steady frame allocates nothing.
         */
        void flush() override {
            if (m_queue.empty()) return;

            m_dispatch.clear();
            m_dispatch.swap(m_queue);

            ++m_flushDepth;
            const size_t n = m_listeners.size();
            for (auto& e : m_dispatch) {
                for (size_t i = 0; i < n; ++i) {
                    if (m_listeners[i].cb) m_listeners[i].cb(e);
                }
            }
            --m_flushDepth;
            admitPending();
        }

    private:
        struct Entry {
            ListenerId id;
            std::function<void(const EventT&)> cb;
        };

        /**
         * @brief Settle the listener list once the outermost dispatch has unwound.
         *
         * Admits what subscribed mid-dispatch and reaps what unsubscribed. Both
         * wait for the same moment and for the same reason: a walk further up
         * the stack holds an index into m_listeners, and neither growing nor
         * shrinking it under that walk is safe.
         */
        void admitPending() {
            if (m_flushDepth != 0) return;

            // Reaped here rather than at remove() for the reason above.
            m_listeners.erase(
                std::remove_if(m_listeners.begin(), m_listeners.end(),
                               [](const Entry& e) { return !e.cb; }),
                m_listeners.end());

            if (m_pending.empty()) return;
            for (Entry& entry : m_pending) m_listeners.push_back(std::move(entry));
            m_pending.clear();
        }

    private:
        std::vector<Entry>  m_listeners;   ///< Active listeners, walked by index during dispatch.
        std::vector<Entry>  m_pending;     ///< Subscribed mid-dispatch; admitted when it unwinds.
        std::vector<EventT> m_queue;       ///< Events awaiting the next flush().
        std::vector<EventT> m_dispatch;    ///< The batch being delivered; swaps with m_queue to keep both buffers.
        ListenerId m_nextId     = 1;       ///< Next listener id to hand out.
        int        m_flushDepth = 0;       ///< >0 while inside emit/flush.
};

} // namespace Vkm::Engine
