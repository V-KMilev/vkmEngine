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
 * The manager keeps a per-type name -> storage index map and guarantees a name
 * is non-empty and unique within its type; a name assigned through `edit()`
 * after `add()` would leave that map holding the old string, and
 * `findByName(newName)` would return nothing for the rest of the session.
 * Assign a name when the asset goes in - `add(asset, name)` - and change one
 * afterwards with `rename(handle, name)`.
 *
 * What the four mean:
 *
 * `name()` is the serializable identity. A scene file records the name, not the
 * handle, and `findByName` resolves it back to a handle on load; a code-
 * generated asset that is not meant to survive serialization still gets one,
 * because add() insists.
 *
 * `version()` is the change counter the backend keys GPU re-uploads on: GLView
 * rebuilds a slot only when this moves, so a slider drag re-uploads one
 * material rather than the cache. `ResourceManager::commit` is what moves it.
 *
 * `uid()` names the asset itself rather than the slot it sits in. A completion
 * that crossed a worker hop compares the uid it was minted against with the one
 * it finds, and so tells its asset apart from a stranger that has since
 * recycled the slot.
 *
 * `isHidden()` marks an asset that pickers, the Asset Browser and the scene
 * saver skip; `ResourceManager::addPrivate` is what sets it.
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
        // Deleted rather than defined. Assigning one asset's base over another
        // silently drops the uid and version the copy constructor deliberately
        // omits, so a caller replacing an asset's contents gets an identity the
        // manager never issued. ResourceManager::swapValue is the door for that,
        // and it is the only one - this deletion is what keeps it so.
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
