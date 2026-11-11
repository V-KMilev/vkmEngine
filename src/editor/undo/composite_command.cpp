#include "undo/composite_command.h"

#include <cstring>

namespace Vkm::Engine {

bool CompositeCommand::tryMerge(Command& incoming) {
    // The label is the literal one call site passes, so it is the group's identity.
    auto* p = dynamic_cast<CompositeCommand*>(&incoming);
    if (!p || std::strcmp(p->m_label, m_label) != 0) return false;
    // A gesture's steps come in the order its first did, each into the held step at its own place:
    // that one is asked first, so a drag over a large selection merges in a pass, not a search each.
    for (size_t i = 0; i < p->m_commands.size(); ++i) {
        std::unique_ptr<Command>& step = p->m_commands[i];
        bool merged = i < m_commands.size() && m_commands[i]->tryMerge(*step);
        for (size_t h = 0; !merged && h < m_commands.size(); ++h) {
            merged = h != i && m_commands[h]->tryMerge(*step);
        }
        if (!merged) m_commands.push_back(std::move(step));
    }
    return true;
}

} // namespace Vkm::Engine
