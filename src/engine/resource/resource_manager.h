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
#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Engine {

/**
 * @brief Open type-erased resource registry with typed handles and generational lifetimes.
 *
 * Any type inheriting from Resource can be stored without modifying this class.
 * Mirrors Scene's design: per type we keep a SlotAllocator (handle lifetime)
 * and a SparseSet<T> (storage), plus name/id indices for O(1) lookup.
 */
class ResourceManager {
    public:
        ResourceManager() = default;
        ~ResourceManager() = default;

        ResourceManager(const ResourceManager& other) = delete;
        ResourceManager& operator=(const ResourceManager& other) = delete;

        ResourceManager(ResourceManager && other) = delete;
        ResourceManager& operator=(ResourceManager && other) = delete;

    public:
        /**
         * @brief Add a new resource to the manager.
         *
         * @tparam ResourceType The resource type (must inherit from Resource).
         * @param resource The resource instance to add (will be moved).
         * @return The handle for the newly added resource.
         */
        template<typename ResourceType>
        auto add(ResourceType && resource) {
            using T = std::remove_cv_t<std::remove_reference_t<ResourceType>>;
            auto& slot = getSlot<T>();

            StorageIndex key = slot.allocator->allocate();
            // Capture the name before move (some types are large).
            std::string indexName = resource.name;
            storageOf<T>(slot).add(key.index, std::forward<ResourceType>(resource));

            // Maintain the per-type name index for O(1) findByName. Unnamed
            // assets don't enter the index (no key to look them up by).
            if (!indexName.empty()) {
                slot.nameIndex.emplace(std::move(indexName), key.index);
            }
            // Same shape for the AssetId index - loaders stamp the GUID on
            // import, scene serializer reads it back via findById<T>.
            const T& inserted = storageOf<T>(slot).get(key.index);
            if (inserted.assetId) {
                slot.idIndex.emplace(inserted.assetId, key.index);
            }
            return Handle<T>{key};
        }

        /// @brief Convenience overload: stamp a name onto the asset before insertion.
        template<typename ResourceType>
        auto add(ResourceType && resource, std::string name) {
            resource.name = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Insert a private asset hidden from user-facing surfaces.
         *
         * Pickers, the Asset Browser and the scene saver filter on
         * Resource::hidden so these never surface to the user or get
         * serialized into a scene save. Today's only caller is the editor
         * (preview primitives, neutral thumbnail materials); the flag is
         * named for the visibility intent, not the consumer.
         *
         * @tparam ResourceType The resource type (must inherit from Resource).
         * @param resource The resource instance to add (will be moved).
         * @param name Stable name to stamp onto the asset.
         * @return Handle for the newly inserted private asset.
         */
        template<typename ResourceType>
        auto addPrivate(ResourceType && resource, std::string name) {
            resource.hidden = true;
            resource.name = std::move(name);
            return add(std::forward<ResourceType>(resource));
        }

        /**
         * @brief Remove a resource by handle.
         */
        template<typename HandleType>
        void remove(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::remove invalid handle");
            // Drop the name->index mapping if the asset registered one.
            const T& res = storageOfConst<T>(slot).get(handle.key.index);
            if (!res.name.empty()) {
                auto it = slot.nameIndex.find(res.name);
                if (it != slot.nameIndex.end() && it->second == handle.key.index) {
                    slot.nameIndex.erase(it);
                }
            }
            if (res.assetId) {
                auto idIt = slot.idIndex.find(res.assetId);
                if (idIt != slot.idIndex.end() && idIt->second == handle.key.index) {
                    slot.idIndex.erase(idIt);
                }
            }
            storageOf<T>(slot).remove(handle.key.index);
            slot.allocator->free(handle.key);
        }

        /**
         * @brief Liveness check: true when @p handle still names a valid
         *        resource of its type.
         *
         * Use this before get/edit on handles that might have been freed
         * since you captured them - chiefly async completions whose
         * source code path is separated from the resource lifetime by a
         * worker hop.
         */
        template<typename HandleType>
        bool isAlive(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return false;
            const auto& slot = *m_slots[id];
            return slot.allocator->has(handle.key);
        }

        /// @brief Get const access to a resource by handle.
        template<typename HandleType>
        const auto& get(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const auto& slot = getSlotConst<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::get invalid handle");
            return storageOfConst<T>(slot).get(handle.key.index);
        }

        /**
         * @brief Get mutable access to a resource for editing by handle.
         *
         * IMPORTANT: do NOT mutate the `name` field through this reference -
         * the per-type findByName index will go stale and findByName(newName)
         * keeps returning nothing. Use rename(handle, newName) instead. Every
         * other field is safe to edit in place; only the name is indexed.
         */
        template<typename HandleType>
        auto& edit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::edit invalid handle");
            return storageOf<T>(slot).get(handle.key.index);
        }

        /**
         * @brief Rename a resource and keep findByName consistent.
         *
         * Direct `edit(h).name = ...` only mutates the asset; the per-type
         * name index won't see the change and findByName(newName) keeps
         * returning nothing. Use this whenever a name is assigned after add().
         */
        template<typename HandleType>
        void rename(const HandleType& handle, std::string newName) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::rename invalid handle");
            auto& res = storageOf<T>(slot).get(handle.key.index);
            if (!res.name.empty()) {
                auto it = slot.nameIndex.find(res.name);
                if (it != slot.nameIndex.end() && it->second == handle.key.index) {
                    slot.nameIndex.erase(it);
                }
            }
            res.name = std::move(newName);
            if (!res.name.empty()) {
                slot.nameIndex[res.name] = handle.key.index;
            }
        }

        /**
         * @brief Commit changes to a resource (bumps its per-asset version;
         *        does NOT bump the global version).
         *
         * The per-asset `version` is what the backend keys GPU re-uploads on:
         * GLView's syncTable rebuilds an entry only when this changes. Global
         * version is reserved for handle-invalidating events (swap / clear /
         * swapSlot / remove) - if commit bumped it, every per-frame slider drag
         * would wipe and re-upload the whole GPU cache to update one material.
         */
        template<typename HandleType>
        void commit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            static_assert(std::is_base_of_v<Resource, T>, "Resource type must inherit from Resource to use commit().");
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::commit invalid handle");
            ++storageOf<T>(slot).get(handle.key.index).version;
        }

        /**
         * @brief Find a resource by its `name` field. Returns a default
         * (invalid) handle if the type is unregistered or no asset matches.
         *
         * O(1) lookup via a per-type name->index map maintained on
         * add/remove. Caveat: the index is populated from the resource's
         * `name` at insertion time; later mutations to `name` via edit()
         * are not reflected. Don't rename assets after registering them,
         * or call removeByName/add() instead.
         */
        template<typename T>
        Handle<T> findByName(const std::string& name) const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return {};
            const auto& slot = *m_slots[id];

            auto it = slot.nameIndex.find(name);
            if (it == slot.nameIndex.end()) return {};
            const uint32_t index = it->second;
            // Defensive: if the entry was removed (shouldn't happen because
            // remove() erases the mapping, but guards against rename-after-
            // add drift), fall back to invalid.
            if (!storageOfConst<T>(slot).contains(index)) return {};
            return Handle<T>{StorageIndex{index, slot.allocator->generationOf(index)}};
        }

        /**
         * @brief Update the idIndex after a caller stamped a fresh AssetId
         *        on an asset that was already add()'d.
         *
         * Loaders that only know the asset's stable identity *after*
         * insertion (folder-loaded materials, where the folder path drives
         * the GUID but the desc-only loadMaterialFromDesc shouldn't depend
         * on it) call this once they've assigned `assetId`. No-op if the
         * asset's assetId is invalid.
         */
        template<typename HandleType>
        void reindexAssetId(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator->has(handle.key), "ResourceManager::reindexAssetId invalid handle");
            const T& res = storageOfConst<T>(slot).get(handle.key.index);
            if (!res.assetId) return;
            slot.idIndex[res.assetId] = handle.key.index;
        }

        /**
         * @brief Find a resource by its `assetId` field.
         *
         * The serialised-identity counterpart to findByName: loaders stamp
         * each on-disk asset with a stable AssetId at import (via the
         * AssetDatabase), and scene files reference assets by GUID. O(1)
         * via a per-type idIndex maintained on add/remove.
         */
        template<typename T>
        Handle<T> findById(AssetId assetId) const {
            if (!assetId) return {};
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return {};
            const auto& slot = *m_slots[id];

            auto it = slot.idIndex.find(assetId);
            if (it == slot.idIndex.end()) return {};
            const uint32_t index = it->second;
            if (!storageOfConst<T>(slot).contains(index)) return {};
            return Handle<T>{StorageIndex{index, slot.allocator->generationOf(index)}};
        }

        /**
         * @brief Visit every live resource of type T as (Handle<T>, const T&).
         *
         * Linear scan - for editor asset pickers / tooling, not hot paths.
         * A no-op when the type is unregistered.
         */
        template<typename T, typename Fn>
        void forEachOfType(Fn&& fn) const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return;
            const auto& slot = *m_slots[id];
            storageOfConst<T>(slot).forEach([&](uint32_t index, const T& res) {
                fn(Handle<T>{StorageIndex{index, slot.allocator->generationOf(index)}}, res);
            });
        }

        /**
         * @brief Drop every resource of every registered type.
         *
         * Used by scene-load flows that want a true cold-start (and by tests).
         * Does not check refcounts - caller is responsible for clearing the
         * scene first so no entity is still pointing at a freed handle.
         */
        void clear() {
            LOG_INFO_C("RESOURCE", "Clear (dropping %zu asset type(s))", m_slots.size());
            m_slots.clear();
        }

        /**
         * @brief Swap the entire asset graph with another ResourceManager.
         *
         * Used by SceneSerializer::load for transactional asset+scene swap:
         * a staging ResourceManager is filled while the live one continues
         * to back the running editor; on full load success this swap +
         * Scene::swap commits both in one phase. On failure the staging
         * is dropped and the live state is untouched, so a malformed
         * scene file no longer orphans newly-loaded assets in the live
         * graph.
         *
         * NOTE: outstanding handles from before the swap are stale - their
         * (index, generation) keys point at slots in the OTHER manager.
         * Editor panels that cache handles to hidden assets must
         * re-acquire on next use; the standard pattern is findByName-then-
         * addPrivate, which works because findByName is O(1) now.
         */
        void swap(ResourceManager& other) noexcept {
            using std::swap;
            swap(m_slots, other.m_slots);
            LOG_INFO_C("RESOURCE", "Swap committed");
        }

        /**
         * @brief Swap the per-type slot for @p T with @p other.
         *
         * Used by SceneSerializer to keep engine-owned asset types (shaders)
         * out of the scene-level swap: a full RM swap would strand cached
         * handles in render passes because the scene-staged RM never had
         * those types populated. Slot exchange is symmetric and works even
         * when either side has no slot for @p T yet.
         */
        template<typename T>
        void swapSlot(ResourceManager& other) noexcept {
            using std::swap;
            TypeId id = typeId<T>();
            const size_t needed = static_cast<size_t>(id) + 1;
            if (m_slots.size() < needed) m_slots.resize(needed);
            if (other.m_slots.size() < needed) other.m_slots.resize(needed);
            swap(m_slots[id], other.m_slots[id]);
        }

    private:
        /// Per-type bundle: lifetime (allocator), storage, name + id indices.
        struct TypedSlot {
            std::unique_ptr<SlotAllocator>  allocator;
            std::unique_ptr<ISparseSet>     storage;
            /// name -> storage index. O(1) findByName backing. Populated
            /// from Resource::name on add(), erased on remove().
            std::unordered_map<std::string, uint32_t> nameIndex;
            /// assetId -> storage index. O(1) findById backing. Populated
            /// from Resource::assetId on add() when valid.
            std::unordered_map<AssetId, uint32_t> idIndex;
        };

        template<typename T>
        TypedSlot& getSlot() {
            TypeId id = typeId<T>();
            if (id >= m_slots.size()) m_slots.resize(id + 1);
            if (!m_slots[id]) {
                m_slots[id] = std::make_unique<TypedSlot>();
                m_slots[id]->allocator = std::make_unique<SlotAllocator>();
                m_slots[id]->storage   = std::make_unique<SparseSet<T>>();
            }
            return *m_slots[id];
        }

        template<typename T>
        const TypedSlot& getSlotConst() const {
            TypeId id = typeId<T>();
            VKM_ASSERT(id < m_slots.size() && m_slots[id], "ResourceManager: type not registered");
            return *m_slots[id];
        }

        template<typename T>
        static SparseSet<T>& storageOf(TypedSlot& slot) {
            return static_cast<SparseSet<T>&>(*slot.storage);
        }

        template<typename T>
        static const SparseSet<T>& storageOfConst(const TypedSlot& slot) {
            return static_cast<const SparseSet<T>&>(*slot.storage);
        }

    private:
        std::vector<std::unique_ptr<TypedSlot>> m_slots;
};

} // namespace Engine
