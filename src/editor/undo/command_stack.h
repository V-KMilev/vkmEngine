#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "undo/command.h"

namespace Vkm::Engine {

class Scene;
class CommandHost;

/**
 * @brief Bounded undo/redo history, owned by EditorState.
 */
class CommandStack {
    public:
        CommandStack();
        ~CommandStack();

        CommandStack(const CommandStack& other) = delete;
        CommandStack& operator=(const CommandStack& other) = delete;

        CommandStack(CommandStack && other) = delete;
        CommandStack& operator=(CommandStack && other) = delete;

        /**
         * @brief Push a freshly applied command, clearing the redo stack.
         *
         * Merges into the top step while its gesture is open and Command::tryMerge agrees.
         *
         * @param cmd The applied change; null is ignored.
         */
        void push(std::unique_ptr<Command> cmd);

        /**
         * @brief Seal the top of the undo stack so nothing pushed later merges into it.
         *
         * Call once the pointer is up and no widget is active: one gesture is one undo step.
         */
        void endGesture() { m_mergeOpen = false; }

        /**
         * @brief Reverse the newest command and move it to the redo stack; no-op when there is none.
         *
         * @param scene Scene the command's reverse operation mutates.
         * @param host  What the command may touch outside the scene (e.g. selection).
         */
        void undo(Scene& scene, CommandHost& host);

        /**
         * @brief Re-apply the newest undone command and move it back to undo; no-op when there is none.
         *
         * @param scene Scene the command's redo operation mutates.
         * @param host  What the command may touch outside the scene (e.g. selection).
         */
        void redo(Scene& scene, CommandHost& host);

        /**
         * @brief Reverse the most recent command and drop it, so no redo brings it back.
         *
         * For a step its own gesture finds unwanted. No-op if the undo stack is empty.
         *
         * @param scene Scene the command's reverse operation mutates.
         * @param host  What the command may touch outside the scene (e.g. selection).
         */
        void retract(Scene& scene, CommandHost& host);

        /// Drop all undo and redo history, a parked one included, for a scene swap that makes it meaningless.
        void clear();

        /**
         * @brief Set the history aside, with the returns noted in @p scene, and record what follows on
         *        an empty one; ignored while parked.
         *
         * For a play session, whose world Stop throws away: its undo cannot reach authored work.
         *
         * @param scene The world the history was made in, as Play found it.
         */
        void park(const Scene& scene);

        /**
         * @brief Discard every step and return since park() and restore the parked ones; no-op when none
         *        is parked.
         *
         * @param scene The world park() was given, put back under the ids it had: the parked returns
         *              name its entities.
         */
        void unpark(const Scene& scene);

        /**
         * @brief Drop the steps that address (Command::addresses) any entity in @p slotIndices.
         *
         * The narrow half of clear(), for an operation that outlives part of the history.
         *
         * @param slotIndices Entity slots whose steps are no longer meaningful.
         */
        void forget(const std::vector<uint32_t>& slotIndices);

        /**
         * @brief Note that the entity a step destroyed as @p was came back as @p now.
         *
         * A reference to @p was that a later undo or redo puts back follows it there (returnOf).
         *
         * @param scene The world it came back in; the returns noted in any other are dropped.
         * @param was   The id the entity was destroyed under.
         * @param now   The id it came back as.
         */
        void noteReturn(const Scene& scene, EntityId was, EntityId now);

        /**
         * @brief The id the entity named @p id came back as last, through every return noted since.
         *
         * @param scene The world @p id names an entity of; a return noted in another is not followed.
         * @param id    An entity's id, as a step holds it.
         * @return @p id when that entity never came back in @p scene; else the last id it came back as,
         *         alive or destroyed again.
         */
        EntityId returnOf(const Scene& scene, EntityId id) const;

        /**
         * @brief The step a push would merge into while its gesture is open, else null.
         *
         * A costly edit may update this step for the rest of a drag instead of pushing each frame.
         */
        const Command* mergeTarget() const {
            return m_mergeOpen && !m_undo.empty() ? m_undo.back().get() : nullptr;
        }

        bool canUndo() const { return !m_undo.empty(); }
        bool canRedo() const { return !m_redo.empty(); }

        /**
         * @brief Label of a command undo would take back.
         *
         * @param back Steps behind the newest: 0 is the next undo.
         * @return The label, or nullptr past the history.
         */
        const char* undoLabel(size_t back = 0) const;

        /**
         * @brief Label of a command redo would put back.
         *
         * @param ahead Steps ahead of the next: 0 is the next redo.
         * @return The label, or nullptr past the history.
         */
        const char* redoLabel(size_t ahead = 0) const;

        size_t undoDepth() const { return m_undo.size(); }
        size_t redoDepth() const { return m_redo.size(); }

        /**
         * @brief Moves on every push (merged or not), undo, redo and change to the history.
         *
         * What a view of the edited world compares, while no session runs, to know it is current. An
         * edit an open step takes in without a push (TerrainEditCommand::apply) does not move it.
         */
        uint64_t revision() const { return m_revision; }

    private:
        static constexpr size_t HISTORY_LIMIT = 200;  ///< Undo steps kept, to bound editor memory.

    private:
        std::vector<std::unique_ptr<Command>> m_undo;
        std::vector<std::unique_ptr<Command>> m_redo;

        bool m_mergeOpen = false;  ///< The top step may still absorb a push: from a push until the gesture
                                   ///< ends or the stack changes otherwise.

        std::vector<std::unique_ptr<Command>> m_parkedUndo;  ///< The authored history while a session runs.
        std::vector<std::unique_ptr<Command>> m_parkedRedo;
        bool                                  m_parked = false;

        uint64_t m_revision = 0;  ///< See revision().

        std::vector<std::pair<EntityId, EntityId>> m_returns;  ///< Each id a step destroyed, and the id it
                                                               ///< came back as, in m_returnsWorld.

        std::vector<std::pair<EntityId, EntityId>> m_parkedReturns;  ///< m_returns as park() found them,
                                                                     ///< naming the world it was given.

        uint64_t m_returnsWorld = 0;  ///< Scene::epoch() of the world m_returns names: a load or a clear
                                      ///< replaces it, its ids numbered afresh, and unpark() names the
                                      ///< one Stop put back.

        bool m_limitLogged = false;  ///< The history limit is by design: said once, not on every push.
};

} // namespace Vkm::Engine
