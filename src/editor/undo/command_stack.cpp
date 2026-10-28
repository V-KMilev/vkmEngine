#define VKM_LOG_CATEGORY "EDITOR"

#include "command/command_stack.h"

#include <algorithm>

#include "logger.h"

namespace Vkm::Engine {

CommandStack::CommandStack()  = default;
CommandStack::~CommandStack() = default;

void CommandStack::push(std::unique_ptr<Command> cmd) {
    if (!cmd) return;

    // A fresh edit invalidates the redo history. Drop it before merge so a
    // failed merge doesn't leave a half-state.
    m_redo.clear();

    if (m_mergeOpen && !m_undo.empty() && m_undo.back()->tryMerge(*cmd)) {
        LOG_VERBOSE("Merged into '%s' (undo size %zu)", m_undo.back()->label(), m_undo.size());
        return;
    }

    const char* label = cmd->label();
    m_undo.push_back(std::move(cmd));

    if (m_undo.size() > HISTORY_LIMIT) {
        const size_t dropped = m_undo.size() - HISTORY_LIMIT;
        m_undo.erase(m_undo.begin(), m_undo.begin() + dropped);
        LOG_WARNING("History limit %zu reached; dropped %zu oldest command(s)", HISTORY_LIMIT, dropped);
    }
    m_mergeOpen = true;
    LOG_VERBOSE("Pushed '%s' (undo size %zu)", label, m_undo.size());
}

void CommandStack::undo(Scene& scene, CommandHost& host) {
    if (m_undo.empty()) return;
    m_mergeOpen = false;
    auto cmd = std::move(m_undo.back());
    m_undo.pop_back();
    LOG_VERBOSE("Undo '%s' (undo %zu -> redo %zu)", cmd->label(), m_undo.size(), m_redo.size() + 1);
    cmd->undo(scene, host);
    m_redo.push_back(std::move(cmd));
}

void CommandStack::redo(Scene& scene, CommandHost& host) {
    if (m_redo.empty()) return;
    m_mergeOpen = false;
    auto cmd = std::move(m_redo.back());
    m_redo.pop_back();
    LOG_VERBOSE("Redo '%s' (redo %zu -> undo %zu)", cmd->label(), m_redo.size(), m_undo.size() + 1);
    cmd->redo(scene, host);
    m_undo.push_back(std::move(cmd));
}

void CommandStack::clear() {
    if (!m_undo.empty() || !m_redo.empty()) {
        LOG_VERBOSE("Cleared (dropped %zu undo + %zu redo)", m_undo.size(), m_redo.size());
    }
    m_undo.clear();
    m_redo.clear();
    m_parkedUndo.clear();
    m_parkedRedo.clear();
    m_parked    = false;
    m_mergeOpen = false;
}

void CommandStack::park() {
    if (m_parked) return;
    m_parkedUndo = std::move(m_undo);
    m_parkedRedo = std::move(m_redo);
    m_undo.clear();
    m_redo.clear();
    m_parked    = true;
    m_mergeOpen = false;
}

void CommandStack::unpark() {
    if (!m_parked) return;
    m_undo = std::move(m_parkedUndo);
    m_redo = std::move(m_parkedRedo);
    m_parkedUndo.clear();
    m_parkedRedo.clear();
    m_parked    = false;
    m_mergeOpen = false;
}

void CommandStack::forget(const std::vector<uint32_t>& slotIndices) {
    const auto outlived = [&](const std::unique_ptr<Command>& cmd) {
        for (const uint32_t slot : slotIndices) {
            if (cmd->addresses(slot)) return true;
        }
        return false;
    };

    const size_t before = m_undo.size() + m_redo.size();
    m_undo.erase(std::remove_if(m_undo.begin(), m_undo.end(), outlived), m_undo.end());
    m_redo.erase(std::remove_if(m_redo.begin(), m_redo.end(), outlived), m_redo.end());

    const size_t dropped = before - (m_undo.size() + m_redo.size());
    if (dropped > 0) {
        LOG_VERBOSE(
            "Forgot %zu step(s) addressing %zu entit%s (undo %zu, redo %zu)",
            dropped,
            slotIndices.size(),
            slotIndices.size() == 1 ? "y" : "ies",
            m_undo.size(),
            m_redo.size()
        );
    }
    m_mergeOpen = false;
}

const char* CommandStack::undoLabel() const {
    return m_undo.empty() ? nullptr : m_undo.back()->label();
}

const char* CommandStack::redoLabel() const {
    return m_redo.empty() ? nullptr : m_redo.back()->label();
}

} // namespace Vkm::Engine
