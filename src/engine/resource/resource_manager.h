#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "l_assert.h"
#include "logger.h"

#include "core/memory/slot_allocator.h"
#include "core/memory/sparse_set.h"
#include "core/memory/types.h"
#include "core/memory/type_registry.h"
#include "resource/asset_ref.h"
#include "resource/asset_type.h"
#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

struct FontAsset;

/**
 * @brief True for the asset types ResourceManager makes a slot for at construction.
 *
 * The library's kinds (VKM_ASSET_KINDS) plus FontAsset, which is baked rather than cooked.
 *
 * @tparam T A Resource subclass.
 */
template<typename T>
inline constexpr bool IS_ENGINE_ASSET = ASSET_TYPE<T> != AssetType::Count || std::is_same_v<T, FontAsset>;

/**
 * @brief Open type-erased resource registry with typed handles and generational lifetimes.
 *
 * Per type, as in Scene: a SlotAllocator, a SparseSet<T> and a name index.
 *
 * Only the engine's asset types (IS_ENGINE_ASSET) are stored, each slot made in vkm_core at
 * construction. A SparseSet<T> carries the vtable of the binary that made it, so a slot a gameplay
 * module made would outlive the module past dlclose; any other type is a compile error. See
 * scripting.md, "Nothing may hold module code across the swap".
 */
class ResourceManager {
    public:
        ResourceManager();
        ~ResourceManager() = default;

        ResourceManager(const ResourceManager& other) = delete;
        ResourceManager& operator=(const ResourceManager& other) = delete;

        ResourceManager(ResourceManager && other) = delete;
        ResourceManager& operator=(ResourceManager && other) = delete;

    public:
        /**
         * @brief Put @p resource in the graph under its name, stamping its identity.
         *
         * The name is the identity (what a scene file stores), so naming one that exists
         * **replaces its contents in place** and returns the existing handle; this keeps adding
         * repeatable across a script reload, where `onStart` runs again. An unnamed asset gets a
         * free name ("asset", "asset (2)", ...) instead. Either way it gets a fresh uid, so an
         * async completion minted against a previous occupant can tell.
         *
         * @tparam ResourceType The asset type, deriving from Resource.
         * @param resource The asset to add (moved from).
         * @return The handle naming the asset - the existing one on a replace.
         */
        template<typename ResourceType>
        auto add(ResourceType && resource) {
            using T = std::remove_cv_t<std::remove_reference_t<ResourceType>>;
            static_assert(
                std::is_base_of_v<Resource, T>,
                "ResourceManager stores only types deriving from Resource."
            );
            auto& slot = getSlot<T>();

            if (resource.m_name.empty()) ensureUniqueName(slot, resource.m_name);

            if (const auto taken = slot.nameIndex.find(resource.m_name); taken != slot.nameIndex.end()) {
                return replaceAt<T>(slot, taken->second, std::forward<ResourceType>(resource));
            }

            StorageIndex key = slot.allocator.allocate();
            std::string indexName = resource.m_name;  // non-empty now
            // Stamped on the stored asset, not the argument: insertion may copy,
            // and a copy is a duplicate that carries no identity of its own.
            Resource& stored = storageOf<T>(slot).add(key.index, std::forward<ResourceType>(resource));
            stored.m_uid     = mintUid();
            stored.m_version = 1;
            slot.nameIndex.emplace(std::move(indexName), key.index);

            return Handle<T>{key};
        }

        /**
         * @brief add() under @p name.
         *
         * @tparam ResourceType The asset type, deriving from Resource.
         * @param resource The asset to add (moved from).
         * @param name Name to stamp before insertion.
         * @return Handle for the asset now standing under @p name.
         */
        template<typename ResourceType>
        auto add(ResourceType && resource, std::string name) {
            resource.m_name = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief add() under @p name, setting Resource::isHidden: kept from the user and scene saves.
         *
         * @tparam ResourceType The asset type, deriving from Resource.
         * @param resource The asset to add (moved from).
         * @param name Name to stamp before insertion.
         * @return Handle for the private asset.
         */
        template<typename ResourceType>
        auto addPrivate(ResourceType && resource, std::string name) {
            resource.m_hidden = true;
            resource.m_name   = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Remove a resource: its name mapping, storage and handle slot.
         *
         * GPU memory is not freed: GLView reclaims a slot when its index is recycled, or all at
         * once when the epoch moves. A stale handle asserts in debug and is a no-op in release:
         * past the generation check everything addresses the slot index alone.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle The resource to remove; must still be live.
         */
        template<typename HandleType>
        void remove(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::remove invalid handle");
            if (!slot.allocator.has(handle.key)) return;

            const T& res = storageOfConst<T>(slot).get(handle.key.index);
            dropNameIndex(slot, res.m_name, handle.key.index);
            storageOf<T>(slot).remove(handle.key.index);
            slot.allocator.free(handle.key);
        }

        /**
         * @brief Whether @p handle still names a live resource of its type.
         *
         * For get/edit on handles that may have been freed since, chiefly in async completions.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle Any value.
         * @return Whether @p handle names a live resource.
         */
        template<typename HandleType>
        bool isAlive(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const TypedSlot* slot = trySlot<T>();
            return slot && slot->allocator.has(handle.key);
        }

        /**
         * @brief The resource @p handle names; asserts it is live (see isAlive(), tryGet()).
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle The resource to fetch.
         * @return The stored resource.
         */
        template<typename HandleType>
        const auto& get(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const auto& slot = getSlotConst<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::get invalid handle");
            return storageOfConst<T>(slot).get(handle.key.index);
        }

        /**
         * @brief The asset @p handle names, or null when it names none.
         *
         * The tolerant get(), as Scene::tryGet: for a handle out of a component or cached state,
         * which may be null or stale.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle Any value; null or stale answers nullptr.
         * @return The asset, or nullptr.
         */
        template<typename HandleType>
        const auto* tryGet(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const TypedSlot* slot = trySlot<T>();
            if (!slot || !slot->allocator.has(handle.key)) return static_cast<const T*>(nullptr);
            return &storageOfConst<T>(*slot).get(handle.key.index);
        }

        /**
         * @brief The asset @p handle names for editing, or null when it names none.
         *
         * The tolerant edit(): commit() afterwards if the backend needs to see the change.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle Any value.
         * @return The asset, or nullptr.
         */
        template<typename HandleType>
        auto* tryEdit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            TypedSlot* slot = m_slots.find<T>();
            if (!slot || !slot->allocator.has(handle.key)) return static_cast<T*>(nullptr);
            return &storageOf<T>(*slot).get(handle.key.index);
        }

        /**
         * @brief Mutable access to the resource @p handle names.
         *
         * Identity stays private to Resource: rename() changes a name, commit() tells the backend
         * the contents changed.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle The resource; must still be live.
         * @return The stored resource.
         */
        template<typename HandleType>
        auto& edit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::edit invalid handle");
            return storageOf<T>(slot).get(handle.key.index);
        }

        /**
         * @brief Rename a resource, keeping findByName consistent.
         *
         * @p newName may come straight from a text field: empty falls back to the generic base and
         * a taken one gets a " (N)" suffix. Unlike add(), a taken name never replaces its holder -
         * renaming A onto B's name is a slip, not a request to destroy B.
         *
         * @param handle The asset to rename.
         * @param newName Desired name; made unique.
         */
        template<typename HandleType>
        void rename(const HandleType& handle, std::string newName) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::rename invalid handle");
            auto& res = storageOf<T>(slot).get(handle.key.index);
            // Before the uniqueness check, so renaming to the current name is not a self-collision.
            dropNameIndex(slot, res.m_name, handle.key.index);
            ensureUniqueName(slot, newName);
            res.m_name = std::move(newName);
            slot.nameIndex[res.m_name] = handle.key.index;
        }

        /**
         * @brief Exchange the contents of the asset at @p handle with @p value, each keeping its
         * identity, and commit.
         *
         * Rebuilds an asset without reissuing it: handles and findByName keep naming it. The
         * value's name, uid, hidden flag and version are traded back rather than riding in, which
         * would break the name index and replay a version the backend has seen; @p value can be
         * removed afterwards like any other asset.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle The asset to rebuild; must still be live.
         * @param value The new contents; receives the old ones.
         */
        template<typename HandleType>
        void swapValue(const HandleType& handle, typename HandleType::resource_t& value) {
            using T = typename HandleType::resource_t;
            static_assert(
                std::is_base_of_v<Resource, T>,
                "Resource type must inherit from Resource to use swapValue()."
            );
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::swapValue invalid handle");
            T& target = storageOf<T>(slot).get(handle.key.index);
            if (&target == &value) return;

            using std::swap;
            swap(target, value);
            swap(target.m_version, value.m_version);
            swap(target.m_uid,     value.m_uid);
            swap(target.m_hidden,  value.m_hidden);
            swap(target.m_name,    value.m_name);
            ++target.m_version;
        }

        /**
         * @brief Bump the asset's version, which GLView::sync keys its GPU re-upload on.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle The changed resource; must still be live.
         */
        template<typename HandleType>
        void commit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            static_assert(
                std::is_base_of_v<Resource, T>,
                "Resource type must inherit from Resource to use commit()."
            );
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::commit invalid handle");
            ++storageOf<T>(slot).get(handle.key.index).m_version;
        }

        /**
         * @brief Find a resource by the name it was added or renamed under, in O(1).
         *
         * @tparam T The resource type.
         * @param name The name to look up.
         * @return The handle, or an invalid one when the type is unregistered or nothing matches.
         */
        template<typename T>
        Handle<T> findByName(const std::string& name) const {
            const TypedSlot* slot = trySlot<T>();
            if (!slot) return {};

            auto it = slot->nameIndex.find(name);
            if (it == slot->nameIndex.end()) return {};
            return Handle<T>{slot->allocator.handleAt(it->second)};
        }

        /**
         * @brief Resolve an authored reference to the asset it names.
         *
         * @code
         * m_clip = resources().find(chime);   // AssetRef<AudioClipAsset> chime;
         * @endcode
         *
         * @tparam T The referenced asset type.
         * @param ref The reference; an empty name answers an invalid handle.
         * @return The handle, or an invalid one when nothing has that name.
         */
        template<typename T>
        Handle<T> find(const AssetRef<T>& ref) const {
            return ref.name.empty() ? Handle<T>{} : findByName<T>(ref.name);
        }

        /**
         * @brief Visit every live resource of type T; a linear scan for tooling, not hot paths.
         *
         * A no-op when the type is unregistered.
         *
         * @tparam T  The resource type.
         * @tparam Fn Callable taking (Handle<T>, const T&).
         * @param fn Called per resource in storage order; must not add or remove a T.
         */
        template<typename T, typename Fn>
        void forEachOfType(Fn&& fn) const {
            const TypedSlot* slot = trySlot<T>();
            if (!slot) return;
            storageOfConst<T>(*slot).forEach([&](uint32_t index, const T& res) {
                fn(Handle<T>{slot->allocator.handleAt(index)}, res);
            });
        }

        /**
         * @brief How many resources of type T the graph holds.
         *
         * @return The count; 0 when the type is unregistered.
         */
        template<typename T>
        size_t countOfType() const {
            const TypedSlot* slot = trySlot<T>();
            return slot ? storageOfConst<T>(*slot).size() : 0;
        }

        /**
         * @brief Drop every resource but the fonts, by swap() with an empty manager.
         *
         * Nothing tracks handle holders: clear the scene first.
         */
        void clear();

        /**
         * @brief Swap the asset graph with another ResourceManager, fonts excepted.
         *
         * Commits a staging manager together with Scene::swap; on failure the staging one is
         * dropped and the live state is untouched. Fonts stay: baked at startup, never in
         * a scene file, and a UIText's font name must keep resolving. Handles from before the swap
         * point into the OTHER manager; a cached hidden asset is re-acquired by findByName, then
         * addPrivate.
         *
         * @param other The manager to trade graphs with.
         */
        void swap(ResourceManager& other) noexcept;

        /**
         * @brief Identity of the current asset graph, bumped by every swap.
         *
         * A new graph reuses indices, generations and version 1, so a backend cache must drop
         * everything when this moves.
         *
         * @return The current graph's epoch; never 0.
         */
        uint64_t epoch() const { return m_epoch; }

    private:
        struct TypedSlot {
            SlotAllocator                   allocator;
            std::unique_ptr<ISparseSet>     storage;
            /// name -> storage index.
            std::unordered_map<std::string, uint32_t> nameIndex;
        };

    private:
        /**
         * @brief Make every engine asset type's slot, from vkm_core - see the class note.
         */
        void makeEngineSlots();

        template<typename T>
        TypedSlot& getSlot() {
            static_assert(
                IS_ENGINE_ASSET<T>,
                "ResourceManager stores the engine's asset types only: a slot for a type a "
                "gameplay module declares would carry the module's code past its unload."
            );
            return m_slots.ensure<T>([] {
                auto slot = std::make_unique<TypedSlot>();
                slot->storage = std::make_unique<SparseSet<T>>();
                return slot;
            });
        }

        /**
         * @brief The slot for @p T, or nullptr if the type was never registered.
         *
         * @tparam T The resource type.
         * @return The slot, or nullptr.
         */
        template<typename T>
        const TypedSlot* trySlot() const {
            return m_slots.find<T>();
        }

        template<typename T>
        const TypedSlot& getSlotConst() const {
            const TypedSlot* slot = trySlot<T>();
            VKM_ASSERT(slot, "ResourceManager: type not registered");
            return *slot;
        }

        template<typename T>
        static SparseSet<T>& storageOf(TypedSlot& slot) {
            return static_cast<SparseSet<T>&>(*slot.storage);
        }

        template<typename T>
        static const SparseSet<T>& storageOfConst(const TypedSlot& slot) {
            return static_cast<const SparseSet<T>&>(*slot.storage);
        }

        /**
         * @brief Erase the mapping for @p name when it still points at @p index.
         *
         * @param slot  The type's slot.
         * @param name  The name to drop.
         * @param index Dense index the name must still point at.
         */
        static void dropNameIndex(TypedSlot& slot, const std::string& name, uint32_t index) {
            auto it = slot.nameIndex.find(name);
            if (it != slot.nameIndex.end() && it->second == index) {
                slot.nameIndex.erase(it);
            }
        }

        /**
         * @brief Overwrite the asset at @p index with @p resource, keeping the slot's identity.
         *
         * Index, generation and name stay. The version moves on rather than restarting, as the
         * backend cache has seen a 1; the uid is fresh, so async completions for the previous
         * occupant can tell. Visibility travels with the contents, so addPrivate() can re-declare.
         *
         * @tparam T The resource type.
         * @tparam ResourceType The incoming value's type, T or a reference to one.
         * @param slot That type's slot.
         * @param index Dense index of the asset being replaced.
         * @param resource The new contents (moved from).
         * @return The handle that already named the asset.
         */
        template <typename T, typename ResourceType>
        Handle<T> replaceAt(TypedSlot& slot, uint32_t index, ResourceType && resource) {
            T& target = storageOf<T>(slot).get(index);

            // First: re-declaring an asset with its own contents hands in target itself, and
            // constructing from it would move the name away.
            const uint64_t version = target.m_version;
            std::string    name    = std::move(target.m_name);

            // Constructed: Resource has no copy assignment.
            T incoming(std::forward<ResourceType>(resource));
            using std::swap;
            swap(target, incoming);

            target.m_name    = std::move(name);
            target.m_uid     = mintUid();
            target.m_version = version + 1;
            return Handle<T>{slot.allocator.handleAt(index)};
        }

        /**
         * @brief Make @p name non-empty and unique in @p slot.
         *
         * Empty becomes "asset"; a taken name gets the lowest free " (N)" suffix. Not for a named
         * add(), which replaces.
         *
         * @param slot The type's slot.
         * @param name Adjusted in place until it is free.
         */
        static void ensureUniqueName(const TypedSlot& slot, std::string& name) {
            if (name.empty()) name = "asset";
            if (slot.nameIndex.find(name) == slot.nameIndex.end()) return;
            const std::string base = name;
            for (int n = 2; ; ++n) {
                std::string candidate = base + " (" + std::to_string(n) + ")";
                if (slot.nameIndex.find(candidate) == slot.nameIndex.end()) {
                    name = std::move(candidate);
                    return;
                }
            }
        }

        /**
         * @brief The next asset uid, unique across the process.
         *
         * Process-wide, as a scene load fills a second manager. Defined in vkm_core, not an inline
         * static: on Windows a module DLL instantiating add() would keep its own counter.
         *
         * @return A uid no asset in this process has held.
         */
        static uint64_t mintUid();

    private:
        TypeRegistry<TypedSlot> m_slots;

        /// Starts at 1, so a cache can hold 0 as "never synced".
        uint64_t m_epoch = 1;
};

} // namespace Vkm::Engine
