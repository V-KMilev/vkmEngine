#pragma once

#include <cstdint>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace Vkm::Engine {

/**
 * @brief Opaque id for a registered listener, returned by subscribe().
 */
using ListenerId = uint32_t;

/**
 * @brief The type-erased base EventBus holds each per-type Bus as.
 */
class IBus {
    public:
        IBus() = default;
        virtual ~IBus() = default;

        IBus(const IBus& other) = delete;
        IBus& operator=(const IBus& other) = delete;

        IBus(IBus && other) = delete;
        IBus& operator=(IBus && other) = delete;

    public:
        /**
         * @brief Set this bus's queued events aside, ready to be delivered.
         *
         * Split from @ref deliver so every bus is drained before any listener
         * runs; otherwise a listener's enqueue would go out this flush or the
         * next depending on bus order.
         */
        virtual void takeQueue() = 0;

        /**
         * @brief Deliver what @ref takeQueue set aside, to this bus's listeners.
         */
        virtual void deliver() = 0;

        /**
         * @brief Whether anything is still listening on this bus.
         *
         * See EventBus::dropIdleBuses().
         *
         * @return True while at least one live listener remains.
         */
        virtual bool hasListeners() const = 0;
};

/**
 * @brief Listener list plus deferred-event queue for a single event type.
 *
 * Driven only through EventBus.
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
         * Mid-dispatch, the entry joins once the outermost dispatch unwinds:
         * appending could reallocate the list and destroy the std::function
         * whose operator() is on the stack.
         *
         * @param cb Called with each event of this type delivered from now on.
         * @return The id remove() takes.
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
         * Callable from a listener, on itself too. Mid-dispatch the entry is
         * only marked dead, reaped by admitPending: erasing would shift the
         * index walk and skip a listener, and clearing the std::function would
         * free the captures of the callback still running. An entry still in
         * m_pending is erased outright, since nothing walks it.
         *
         * @param id What subscribe() returned.
         * @return true if it was found - erased, or marked dead mid-dispatch.
         */
        bool remove(ListenerId id) {
            for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
                if (it->id != id) continue;

                if (m_flushDepth != 0) it->alive = false;
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
         *
         * @param event Passed to each listener.
         */
        void emit(const EventT& event) {
            DispatchScope scope(*this);
            const size_t n = m_listeners.size();
            for (size_t i = 0; i < n; ++i) {
                if (m_listeners[i].alive) m_listeners[i].cb(event);
            }
        }

        /**
         * @brief Buffer @p event for delivery on the next flush().
         *
         * @param event Event to queue; moved in.
         */
        void enqueue(EventT event) {
            m_queue.push_back(std::move(event));
        }

        /**
         * @brief Swap the queue aside so what a listener enqueues cannot join it.
         *
         * Swaps with a retained member, so a steady frame allocates nothing.
         */
        void takeQueue() override {
            m_dispatch.clear();
            m_dispatch.swap(m_queue);
        }

        /**
         * @brief Deliver the batch takeQueue set aside to every current listener.
         *
         * An event a listener enqueues now fires on the next flush, whatever its type.
         */
        void deliver() override {
            if (m_dispatch.empty()) return;

            DispatchScope scope(*this);
            const size_t n = m_listeners.size();
            for (auto& e : m_dispatch) {
                for (size_t i = 0; i < n; ++i) {
                    if (m_listeners[i].alive) m_listeners[i].cb(e);
                }
            }
        }

        /**
         * @brief Whether any live listener remains, mid-dispatch removals aside.
         *
         * @return True while at least one live entry remains.
         */
        bool hasListeners() const override {
            for (const Entry& e : m_listeners) {
                if (e.alive) return true;
            }
            return !m_pending.empty();
        }

    private:
        struct Entry {
            ListenerId id;
            std::function<void(const EventT&)> cb;
            bool alive = true;  ///< Cleared by a mid-dispatch remove().
        };

        /**
         * @brief One level of dispatch, closed however the walk leaves.
         *
         * Closed even if a listener throws, or every later subscribe would wait in
         * m_pending forever.
         */
        class DispatchScope {
            public:
                explicit DispatchScope(Bus& bus) : m_bus(bus) { ++m_bus.m_flushDepth; }
                ~DispatchScope() {
                    --m_bus.m_flushDepth;
                    m_bus.admitPending();
                }

                DispatchScope(const DispatchScope& other) = delete;
                DispatchScope& operator=(const DispatchScope& other) = delete;

                DispatchScope(DispatchScope && other) = delete;
                DispatchScope& operator=(DispatchScope && other) = delete;

            private:
                Bus& m_bus;
        };

        /**
         * @brief Settle the listener list once the outermost dispatch has unwound.
         *
         * Admits mid-dispatch subscribers and reaps the dead; both wait because a
         * walk up the stack holds an index into m_listeners.
         */
        void admitPending() {
            if (m_flushDepth != 0) return;

            const auto dead = [](const Entry& e) { return !e.alive; };
            m_listeners.erase(
                std::remove_if(m_listeners.begin(), m_listeners.end(), dead),
                m_listeners.end()
            );

            if (m_pending.empty()) return;
            for (Entry& entry : m_pending) m_listeners.push_back(std::move(entry));
            m_pending.clear();
        }

    private:
        std::vector<Entry>  m_listeners;   ///< Walked by index during dispatch.
        std::vector<Entry>  m_pending;     ///< Subscribed mid-dispatch.
        std::vector<EventT> m_queue;       ///< Awaiting the next flush().
        std::vector<EventT> m_dispatch;    ///< The batch being delivered.
        ListenerId m_nextId     = 1;
        int        m_flushDepth = 0;       ///< >0 while inside emit/flush.
};

} // namespace Vkm::Engine
