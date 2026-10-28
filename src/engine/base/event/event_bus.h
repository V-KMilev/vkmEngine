#pragma once

#include <cstddef>
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
 * Owned by the Engine and carried on FrameContext. Main-thread only. Subscribing
 * and unsubscribing inside a callback are allowed; docs/reference/events.md has
 * the delivery rules.
 */
class EventBus {
    public:
        EventBus() = default;
        ~EventBus() = default;

        EventBus(const EventBus& other) = delete;
        EventBus& operator=(const EventBus& other) = delete;

        EventBus(EventBus && other) = delete;
        EventBus& operator=(EventBus && other) = delete;

    public:
        /**
         * @brief Register a callback for events of type EventT.
         *
         * @tparam EventT Event type listened for.
         * @param callback Called with each EventT emitted or flushed from now on.
         * @return ListenerId for a later unsubscribe().
         */
        template<typename EventT>
        ListenerId subscribe(std::function<void(const EventT&)> callback) {
            return bus<EventT>().subscribe(std::move(callback));
        }

        /**
         * @brief Remove a registered listener.
         *
         * @tparam EventT Event type it was subscribed to.
         * @param id What subscribe() returned.
         * @return true if it was found and removed.
         */
        template<typename EventT>
        bool unsubscribe(ListenerId id) {
            auto* b = findBus<EventT>();
            return b ? b->remove(id) : false;
        }

        /**
         * @brief Fire @p event synchronously to every listener now.
         *
         * @tparam EventT Event type; no bus is created when nothing listens.
         * @param event Passed to each listener.
         */
        template<typename EventT>
        void emit(const EventT& event) {
            if (auto* b = findBus<EventT>()) b->emit(event);
        }

        /**
         * @brief Queue @p event for delivery on the next flush().
         *
         * @tparam EventT Event type.
         * @param event Event to queue; moved in.
         */
        template<typename EventT>
        void enqueue(EventT event) {
            bus<EventT>().enqueue(std::move(event));
        }

        /**
         * @brief Drain every per-type queue to its listeners.
         *
         * Engine::run calls it at the top of Simulation, once per tick and once
         * per frame. A nested call from a listener is ignored.
         */
        void flush();

        /**
         * @brief Drop every bus nothing listens to any more.
         *
         * For just before a gameplay module is unmapped (see
         * ScriptModule::releaseRegistrations): a Bus<EventT> for a module's event
         * holds the module's vtable, so a flush() after reload would call into
         * unmapped memory. A bus with a listener stays, so drop those first; an
         * idle bus has nowhere to deliver, so nothing is lost.
         */
        void dropIdleBuses();

        /**
         * @brief How many event types have a bus right now.
         *
         * @return The number of live buses.
         */
        std::size_t busCount() const { return m_buses.count(); }

    private:
        /**
         * @brief Return the bus for EventT, creating it on first use.
         *
         * @tparam EventT Event type.
         * @return The bus, created empty if none existed.
         */
        template<typename EventT>
        Bus<EventT>& bus() {
            IBus& base = m_buses.ensure<EventT>([] { return std::make_unique<Bus<EventT>>(); });
            return static_cast<Bus<EventT>&>(base);
        }

        /**
         * @brief Return the bus for EventT, or nullptr if none exists yet.
         *
         * @tparam EventT Event type.
         * @return The bus, or nullptr.
         */
        template<typename EventT>
        Bus<EventT>* findBus() {
            return static_cast<Bus<EventT>*>(m_buses.find<EventT>());
        }

    private:
        TypeRegistry<IBus> m_buses;

        std::vector<IBus*> m_active;  ///< flush()'s snapshot, kept to avoid allocating.

        bool m_flushing = false;
};

/**
 * @brief The sending half of an EventBus: emit and enqueue, and no subscribe.
 *
 * What Behavior::events() hands a behavior, which must listen through
 * Behavior::subscribe: a listener on the bus itself would outlive the behavior
 * and its module, and a flush after reload would call unmapped code.
 *
 * Copies freely; valid as long as the bus.
 */
class EventSender {
    public:
        explicit EventSender(EventBus& bus) : m_bus(&bus) {}
        ~EventSender() = default;

        EventSender(const EventSender& other) = default;
        EventSender& operator=(const EventSender& other) = default;

        EventSender(EventSender && other) = default;
        EventSender& operator=(EventSender && other) = default;

        /**
         * @brief Fire @p event synchronously to every listener now (EventBus::emit).
         *
         * @tparam EventT Event type.
         * @param event Passed to each listener.
         */
        template<typename EventT>
        void emit(const EventT& event) { m_bus->emit(event); }

        /**
         * @brief Queue @p event for delivery on the next flush (EventBus::enqueue).
         *
         * @tparam EventT Event type.
         * @param event Event to queue; moved in.
         */
        template<typename EventT>
        void enqueue(EventT event) { m_bus->enqueue(std::move(event)); }

    private:
        EventBus* m_bus;
};

} // namespace Vkm::Engine
