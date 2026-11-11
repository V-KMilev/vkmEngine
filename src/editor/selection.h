#pragma once

#include <algorithm>
#include <vector>

#include "ecs/entity.h"
#include "ecs/scene.h"

namespace Vkm::Engine {

/**
 * @brief The keys held as a click lands, which decide what it does to the selection.
 */
struct Modifiers {
    bool ctrl  = false;
    bool shift = false;
};

/**
 * @brief What is selected: entities in the order picked, the active one among them, or the World.
 *
 * Never entities and the World together, and the active entity is always one of the entities. The
 * index by slot stores generations, so a reused slot cannot answer for its old occupant.
 */
class Selection {
    public:
        Selection() = default;
        ~Selection() = default;

        Selection(const Selection& other) = default;
        Selection& operator=(const Selection& other) = default;

        Selection(Selection && other) = default;
        Selection& operator=(Selection && other) = default;

    public:
        /**
         * @brief Replace the whole selection with @p id.
         *
         * @param id The entity to select.
         */
        void select(EntityId id) {
            clear();
            add(id);
        }

        /**
         * @brief Add @p id to the selection and make it the active entity.
         *
         * @param id The entity to add; already selected, it only becomes active.
         */
        void add(EntityId id) {
            if (!contains(id)) {
                m_entities.push_back(id);
                if (id.slot() >= m_bySlot.size()) m_bySlot.resize(id.slot() + 1);
                m_bySlot[id.slot()] = id;
            }
            m_active = id;
            m_world  = false;
        }

        /**
         * @brief Take @p id out if it is selected, else add it.
         *
         * @param id The entity toggled; taken out while active, the active entity falls back to the
         *           last one still selected.
         */
        void toggle(EntityId id) {
            if (!contains(id)) {
                add(id);
                return;
            }
            drop(id);
            m_world = false;
        }

        /**
         * @brief A click on @p id: plain selects it alone, Ctrl toggles it, Shift adds it.
         *
         * @param id   The entity clicked; null for empty space, which clears unless a key is held.
         * @param keys The keys held.
         */
        void click(EntityId id, Modifiers keys) {
            if (!id) {
                if (!keys.ctrl && !keys.shift) clear();
            } else if (keys.ctrl) {
                toggle(id);
            } else if (keys.shift) {
                add(id);
            } else {
                select(id);
            }
        }

        /// The World, in place of any entity.
        void selectWorld() {
            clear();
            m_world = true;
        }

        /// Nothing, the World neither.
        void clear() {
            for (const EntityId id : m_entities) m_bySlot[id.slot()] = {};
            m_entities.clear();
            m_active = {};
            m_world  = false;
        }

        /**
         * @brief Make @p id, already selected, the active entity.
         *
         * @param id A member of the selection; ignored when it is not one.
         */
        void makeActive(EntityId id) {
            if (contains(id)) m_active = id;
        }

        /**
         * @brief Drop every selected entity the scene no longer holds.
         *
         * Deletes and scene swaps leave dead ids behind; the active entity falls back to the last one
         * still selected.
         *
         * @param scene The scene the selection names entities of.
         */
        void prune(const Scene& scene) {
            std::vector<EntityId> dead;
            for (const EntityId id : m_entities) {
                if (!scene.isAlive(id)) dead.push_back(id);
            }
            for (const EntityId id : dead) drop(id);
        }

        /// Whether @p id is selected: one comparison, by the slot index.
        bool contains(EntityId id) const {
            return id && id.slot() < m_bySlot.size() && m_bySlot[id.slot()] == id;
        }

        /// The active entity, last clicked; null when none.
        EntityId active() const { return m_active; }

        /// Every selected entity, in the order picked, the active one included.
        const std::vector<EntityId>& entities() const { return m_entities; }

        /// Whether the World is selected, in place of any entity.
        bool world() const { return m_world; }

    private:
        /// Take @p id, which is selected, out; the active entity falls back to the last left.
        void drop(EntityId id) {
            m_entities.erase(std::find(m_entities.begin(), m_entities.end(), id));
            m_bySlot[id.slot()] = {};
            if (m_active == id) m_active = m_entities.empty() ? EntityId{} : m_entities.back();
        }

    private:
        std::vector<EntityId> m_entities;       ///< Every selected entity, the active one included.
        std::vector<EntityId> m_bySlot;         ///< The selected id at each slot, null where none is.
        EntityId              m_active;         ///< Always in m_entities when set.
        bool                  m_world = false;
};

} // namespace Vkm::Engine
