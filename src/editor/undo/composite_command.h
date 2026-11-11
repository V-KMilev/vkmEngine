#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "undo/command.h"

namespace Vkm::Engine {

/**
 * @brief Already-applied commands undone and redone as one step.
 *
 * A same-label composite pushed in the same gesture merges in, each step into the held step that
 * accepts it, so a drag stays one step. No two steps of one group may edit the same component of
 * the same entity.
 */
class CompositeCommand : public Command {
    public:
        explicit CompositeCommand(const char* label) : m_label(label) {}
        ~CompositeCommand() override = default;

        CompositeCommand(const CompositeCommand& other) = delete;
        CompositeCommand& operator=(const CompositeCommand& other) = delete;

        CompositeCommand(CompositeCommand && other) = delete;
        CompositeCommand& operator=(CompositeCommand && other) = delete;

    public:
        /**
         * @brief Append an already-applied sub-command.
         *
         * @param cmd The step; redo replays in append order and undo in reverse.
         */
        void add(std::unique_ptr<Command> cmd) { m_commands.push_back(std::move(cmd)); }

        bool empty() const { return m_commands.empty(); }

        void redo(Scene& scene, CommandHost& host) override {
            for (auto& c : m_commands) c->redo(scene, host);
        }
        void undo(Scene& scene, CommandHost& host) override {
            for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
                (*it)->undo(scene, host);
        }
        const char* label() const override { return m_label; }
        bool tryMerge(Command& incoming) override;
        bool addresses(uint32_t slotIndex) const override {
            for (const auto& c : m_commands) {
                if (c->addresses(slotIndex)) return true;
            }
            return false;
        }

    private:
        std::vector<std::unique_ptr<Command>> m_commands;
        const char*                           m_label;
};

} // namespace Vkm::Engine
