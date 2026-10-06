#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Vkm::Engine {

/**
 * @brief Base class for every asset type, carrying the identity the ResourceManager indexes by.
 *
 * Only the manager writes the identity fields: name an asset with `add(asset, name)`, change it
 * with `rename(handle, name)`. A subclass is a plain data struct of bare public members.
 */
class Resource {
    public:
        // Out of line: destroying and cloning the source needs the full json type.
        Resource();
        ~Resource();

        /**
         * @brief Duplicate @p other's contents and name, but not its identity.
         *
         * Carries the contents, name and source descriptor; not the uid or version, which `add()`
         * stamps afresh.
         *
         * @param other The asset to duplicate.
         */
        Resource(const Resource& other);
        // Deleted: it would drop the uid and version. Replace contents through the manager -
        // add() under a taken name, or swapValue.
        Resource& operator=(const Resource& other) = delete;

        Resource(Resource && other) noexcept;
        Resource& operator=(Resource && other) noexcept;

    public:
        /// Serializable identity; non-empty and unique within its type once added.
        const std::string& name() const noexcept { return m_name; }

        /// Process-unique id stamped by add(); names the asset, not the slot it sits in.
        uint64_t uid() const noexcept { return m_uid; }

        /// Change counter the backend keys GPU re-uploads on; moved by commit().
        uint64_t version() const noexcept { return m_version; }

        /// Added by ResourceManager::addPrivate: not user-facing, not saved.
        bool isHidden() const noexcept { return m_hidden; }

        bool hasSource() const noexcept { return m_source != nullptr; }

        /**
         * @brief Mutable source JSON, allocating a null document if there is none.
         *
         * Callers must #include <nlohmann/json.hpp>.
         *
         * @return The source descriptor.
         */
        nlohmann::json& sourceJson();

        /**
         * @brief Const source JSON; asserts hasSource(), so guard the call with it.
         *
         * @return The existing source descriptor.
         */
        const nlohmann::json& sourceJson() const;

    private:
        friend class ResourceManager;

    private:
        uint64_t    m_version = 1;
        uint64_t    m_uid     = 0;
        std::string m_name;
        bool        m_hidden  = false;

        /// Origin descriptor, lazy-allocated.
        std::unique_ptr<nlohmann::json> m_source;
};

} // namespace Vkm::Engine
