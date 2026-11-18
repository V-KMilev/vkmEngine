#pragma once

#include <cstdint>

namespace Vkm::Engine {

class Scene;
class CommandHost;

/**
 * @brief Reversible editor mutation.
 *
 * Pushed onto the CommandStack after the change is already applied. Undo and
 * redo do not mark the scene unsaved; EditorActions::undo and redo do.
 */
class Command {
    public:
        Command() = default;
        virtual ~Command() = default;

        Command(const Command& other) = delete;
        Command& operator=(const Command& other) = delete;

        Command(Command && other) = delete;
        Command& operator=(Command && other) = delete;

    public:
        /**
         * @brief Re-apply the change after an undo.
         *
         * @param scene Replayed against.
         * @param host  What the step may touch outside the scene.
         */
        virtual void redo(Scene& scene, CommandHost& host) = 0;

        /**
         * @brief Reverse the previously applied change.
         *
         * @param scene Mutated by the reverse operation.
         * @param host  What the step may touch outside the scene.
         */
        virtual void undo(Scene& scene, CommandHost& host) = 0;

        /**
         * @brief Short name for the Edit menu's Undo/Redo entries.
         *
         * @return Text of static storage, a literal or a static string's c_str(): CommandStack::undoLabel()
         *         hands it out past the command's life.
         */
        virtual const char* label() const = 0;

        /**
         * @brief Optionally absorb @p incoming, pushed onto @c this within one gesture.
         *
         * Test identity only: CommandStack::endGesture enforces the gesture boundary.
         *
         * @param incoming Being pushed.
         * @return true if merged, so the stack discards @p incoming.
         */
        virtual bool tryMerge(Command& incoming) { return false; }

        /**
         * @brief Whether undoing or redoing this step lands on the entity in slot @p slotIndex.
         *
         * Lets CommandStack::forget drop outlived steps; keyed by slot, as a destroy-and-resurrect keeps it.
         *
         * @param slotIndex Entity slot asked about.
         * @return true when this step's effect lands on that entity.
         */
        virtual bool addresses(uint32_t slotIndex) const { return false; }
};

} // namespace Vkm::Engine
