#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace Vkm::Engine {

/**
 * @brief Base class for every asset type, carrying the identity the
 * ResourceManager indexes assets by.
 *
 * The four identity fields are the manager's to write and everyone else's to
 * read, which is why they are private with ResourceManager as the one friend.
 * The manager keeps a per-type name -> storage index map, so a name assigned
 * through `edit()` after `add()` would leave that map holding the old string and
 * `findByName` would answer nothing for the rest of the session: name an asset
 * when it goes in - `add(asset, name)` - and change one afterwards with
 * `rename(handle, name)`.
 *
 * A subclass is a plain data struct - bare public members of its own - loaded,
 * saved and looked up generically through this base.
 */
class Resource {
    public:
        // Rule-of-5 out-of-line: the source unique_ptr<json> needs the
        // full json type (only forward-declared here) to destruct +
        // copy-clone, so the special members are defined in resource.cpp.
        Resource();
        ~Resource();

        /**
         * @brief Duplicate @p other's contents and name, but not its identity.
         *
         * A copy is a DUPLICATE, not the same asset twice: it carries the
         * contents, the name and the source descriptor, and neither the uid nor
         * the version, because those belong to the instance the manager holds
         * and `add()` stamps both afresh. The name still has to become distinct
         * within its type, which `add()` does by suffixing - or the caller does
         * by passing one.
         */
        Resource(const Resource& other);
        // Deleted rather than defined: assigning one asset's base over another
        // drops the uid and version the copy constructor omits, so the result is
        // an identity the manager never issued. swapValue is the only door.
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

        /// True for assets filtered from pickers, the Asset Browser and scene save.
        bool isHidden() const noexcept { return m_hidden; }

        bool hasSource() const noexcept { return m_source != nullptr; }

        /**
         * @brief Mutable access to the source JSON, allocating an empty object
         * if the slot is null. Callers must #include <nlohmann/json.hpp>.
         */
        nlohmann::json&       sourceJson();

        /**
         * @brief Const access to the source JSON.
         *
         * Asserts hasSource() rather than allocating, since a const object
         * cannot lazily create the slot; guard the call with hasSource().
         *
         * @return Const reference to the existing source descriptor JSON.
         */
        const nlohmann::json& sourceJson() const;

    private:
        friend class ResourceManager;

    private:
        uint64_t    m_version = 1;
        uint64_t    m_uid     = 0;
        std::string m_name;
        bool        m_hidden  = false;

        /// Origin descriptor JSON, lazy-allocated.
        std::unique_ptr<nlohmann::json> m_source;
};

} // namespace Vkm::Engine
