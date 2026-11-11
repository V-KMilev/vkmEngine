#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/container/storage_index.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

/**
 * @brief What a gesture asks for and something drawn later in the frame, or the next one, does.
 */
enum class Ask : uint8_t {
    ImportModel,    ///< ModelImportDialog's picker.
    PlacePrefab,    ///< PlacePrefabDialog's picker.
    ReloadScripts,  ///< The gameplay module reloaded (ReloadWatcher).
    RestartPlay,    ///< A game asked to start its session over.
    StopPlay,       ///< Stop, on the playbar or by its key.
    RecoverScene,   ///< Recover, at the crash copy's prompt.
    OpenProject,    ///< OpenProjectDialog.
    ResetLayout,    ///< The default layout, cut before the dockspace.
    HideEditor,     ///< The editor's UI hidden, as its key does.
    RevealTerrain,  ///< The Terrain window brought to the front.
    RevealConsole,  ///< The bottom row brought forward on its Console.
    Count
};

/**
 * @brief An asset's own window asked for: which kind, and which one of it (AssetEditors).
 */
struct AssetToEdit {
    AssetType    type;
    StorageIndex key;
};

/**
 * @brief NewProjectDialog asked for, copying a project.
 */
struct NewProject {
    std::string from;  ///< The project copied; empty for templates/default.
};

/**
 * @brief A `vkm` run asked of BuildController.
 */
struct VkmRun {
    std::vector<std::string> words;  ///< What follows `vkm` on its command line.
    std::string              opens;  ///< A project to open once it succeeds, as after `vkm new`; empty for
                                     ///< none.
};

/**
 * @brief The asks made this frame and not yet answered: an Ask, or one of the asks that carry what they
 *        ask for (AssetToEdit, NewProject, VkmRun).
 *
 * A menu closes the frame its item is clicked, taking any modal opened in it, so an item asks and a
 * dialog drawn at the root window's scope answers. A second ask before the answer replaces the first.
 */
class Requests {
    public:
        Requests() = default;
        ~Requests() = default;

        Requests(const Requests& other) = default;
        Requests& operator=(const Requests& other) = default;

        Requests(Requests && other) = default;
        Requests& operator=(Requests && other) = default;

    public:
        void ask(Ask what) { m_asked.set(bit(what)); }

        /// Ask for what @p what carries.
        template<typename Carried>
        void ask(Carried what) { std::get<std::optional<Carried>>(m_carried) = std::move(what); }

        /// Whether @p what was asked for; true once per ask.
        bool take(Ask what) {
            const bool asked = m_asked.test(bit(what));
            m_asked.reset(bit(what));
            return asked;
        }

        /// What an ask of @p Carried carried, once per ask; none while there is none.
        template<typename Carried>
        std::optional<Carried> take() {
            return std::exchange(std::get<std::optional<Carried>>(m_carried), std::nullopt);
        }

    private:
        static size_t bit(Ask what) { return static_cast<size_t>(what); }

    private:
        std::bitset<static_cast<size_t>(Ask::Count)>                                             m_asked;
        std::tuple<std::optional<AssetToEdit>, std::optional<NewProject>, std::optional<VkmRun>> m_carried;
};

} // namespace Vkm::Engine
