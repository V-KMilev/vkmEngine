#pragma once

#include <atomic>
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
 * and a SparseSet<T> (storage), plus a name index for O(1) lookup.
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
            static_assert(std::is_base_of_v<Resource, T>, "ResourceManager stores only types deriving from Resource.");
            auto& slot = getSlot<T>();

            // The name is the asset's serializable identity, so it has to be
            // non-empty and unique within its type for findByName to be an
            // unambiguous key.
            ensureUniqueName(slot, resource.name);

            StorageIndex key = slot.allocator.allocate();
            std::string indexName = resource.name;  // unique + non-empty now
            // Stamped on the stored asset rather than the argument: insertion may
            // copy, and a copy is a duplicate that carries no identity of its own.
            storageOf<T>(slot).add(key.index, std::forward<ResourceType>(resource)).uid = ++s_nextUid;
            slot.nameIndex.emplace(std::move(indexName), key.index);

            return Handle<T>{key};
        }

        /**
         * @brief Convenience overload: stamp a name onto the asset before insertion.
         *
         * @tparam ResourceType The resource type (must inherit from Resource).
         * @param resource The resource instance to add (will be moved).
         * @param name Stable name to assign before the asset is inserted.
         * @return Handle for the newly inserted asset.
         */
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
         * @brief Remove a resource by handle: drops its name mapping, its storage
         *        and its handle slot.
         *
         * The backend mirror is not dropped here and there is no signal that it
         * should be: GLView reclaims a slot when a new asset recycles the index,
         * or all of them at once when the epoch moves. Deleting assets without
         * adding replacements holds their GPU memory until one of those happens.
         *
         * Everything after the generation check addresses the resource by slot
         * index alone, which a stale handle still names correctly, so a handle
         * whose slot has been recycled would drop the current occupant's name
         * mapping and storage. Removing an already-removed handle is a no-op.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle Handle naming the resource to remove; must still be live.
         */
        template<typename HandleType>
        void remove(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::remove invalid handle");
            if (!slot.allocator.has(handle.key)) return;

            const T& res = storageOfConst<T>(slot).get(handle.key.index);
            dropNameIndex(slot, res.name, handle.key.index);
            storageOf<T>(slot).remove(handle.key.index);
            slot.allocator.free(handle.key);
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
            const TypedSlot* slot = trySlot<T>();
            return slot && slot->allocator.has(handle.key);
        }

        /**
         * @brief Get const access to a resource by handle.
         *
         * Asserts the handle still names a live resource; use isAlive() first
         * for handles that may have been freed.
         *
         * @tparam HandleType Handle type identifying the resource type.
         * @param handle Handle naming the resource to fetch.
         * @return Const reference to the stored resource.
         */
        template<typename HandleType>
        const auto& get(const HandleType& handle) const {
            using T = typename HandleType::resource_t;
            const auto& slot = getSlotConst<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::get invalid handle");
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
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::edit invalid handle");
            return storageOf<T>(slot).get(handle.key.index);
        }

        /**
         * @brief Rename a resource and keep findByName consistent.
         *
         * Direct `edit(h).name = ...` only mutates the asset; the per-type
         * name index won't see the change and findByName(newName) keeps
         * returning nothing. Use this whenever a name is assigned after add().
         *
         * Holds the same non-empty + unique-per-type guarantee add() gives, so
         * @p newName may come straight from a text field: an empty string falls
         * back to the generic base and a taken one picks up a " (N)" suffix.
         * Two assets sharing a name would share a serialized identity, and the
         * one that lost the index would be unreachable for the session.
         *
         * @param handle Handle naming the asset to rename.
         * @param newName Desired name; adjusted in place to keep it unique.
         */
        template<typename HandleType>
        void rename(const HandleType& handle, std::string newName) {
            using T = typename HandleType::resource_t;
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::rename invalid handle");
            auto& res = storageOf<T>(slot).get(handle.key.index);
            // Drop the old mapping before the uniqueness check, so renaming an
            // asset to the name it already holds is a no-op rather than a
            // collision with itself.
            dropNameIndex(slot, res.name, handle.key.index);
            ensureUniqueName(slot, newName);
            res.name = std::move(newName);
            slot.nameIndex[res.name] = handle.key.index;
        }

        /**
         * @brief Commit changes to a resource by bumping its per-asset version.
         *
         * The per-asset `version` is what the backend keys GPU re-uploads on:
         * GLView's syncTable rebuilds an entry only when this changes. commit()
         * touches only the one asset's version, so a per-frame slider drag
         * re-uploads just that material instead of the whole GPU cache.
         */
        template<typename HandleType>
        void commit(const HandleType& handle) {
            using T = typename HandleType::resource_t;
            static_assert(std::is_base_of_v<Resource, T>, "Resource type must inherit from Resource to use commit().");
            auto& slot = getSlot<T>();
            VKM_ASSERT(slot.allocator.has(handle.key), "ResourceManager::commit invalid handle");
            ++storageOf<T>(slot).get(handle.key.index).version;
        }

        /**
         * @brief Find a resource by its `name` field.
         *
         * Returns a default (invalid) handle if the type is unregistered or no
         * asset matches.
         *
         * O(1) lookup via a per-type name->index map maintained on
         * add/remove/rename. A `name` mutated directly through edit() is not
         * reflected there; rename(handle, newName) keeps the index consistent.
         */
        template<typename T>
        Handle<T> findByName(const std::string& name) const {
            const TypedSlot* slot = trySlot<T>();
            if (!slot) return {};

            auto it = slot->nameIndex.find(name);
            if (it == slot->nameIndex.end()) return {};
            const uint32_t index = it->second;
            // Defensive: if the entry was removed (shouldn't happen because
            // remove() erases the mapping, but guards against rename-after-
            // add drift), fall back to invalid.
            if (!storageOfConst<T>(*slot).contains(index)) return {};
            return Handle<T>{slot->allocator.handleAt(index)};
        }

        /**
         * @brief Visit every live resource of type T as (Handle<T>, const T&).
         *
         * Linear scan - for editor asset pickers / tooling, not hot paths.
         * A no-op when the type is unregistered.
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
         * @brief Drop every resource of every registered type.
         *
         * The cold-start counterpart to swap(): where swap() hands the graph to
         * another manager, this ends it outright. Bumps the epoch for the same
         * reason swap() does - whatever graph is built next restarts at the same
         * indices, generations and versions, so a backend cache has no other way
         * to tell it from the one that just went away.
         *
         * Does not check refcounts - the caller clears the scene first so no
         * entity is still pointing at a freed handle.
         */
        void clear() {
            LOG_INFO_C("RESOURCE", "Clear (dropping %zu asset type(s))", m_slots.size());
            m_slots.clear();
            ++m_epoch;
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
            ++m_epoch;
            ++other.m_epoch;
            LOG_INFO_C("RESOURCE", "Swap committed");
        }

        /**
         * @brief Swap the per-type slot for @p T with @p other.
         *
         * Used by SceneSerializer to keep engine-owned asset types (fonts)
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
            ++m_epoch;
            ++other.m_epoch;
        }

        /**
         * @brief Identity of the current asset graph, bumped by every swap.
         *
         * Per-asset `version` tracks edits *within* one graph; it cannot detect a
         * wholesale graph replacement (scene load, editor play-stop restore),
         * because the incoming graph is freshly built - its handles restart at
         * the same indices and generations, and its assets at version 1. To a
         * cache keyed on (index, generation, version) the new graph is therefore
         * indistinguishable from the old one, and stale GPU data survives the
         * swap.
         *
         * A backend cache must compare this epoch and drop everything when it
         * moves, then repopulate from the new graph.
         */
        uint64_t epoch() const { return m_epoch; }

    private:
        /**
         * @brief Per-type bundle: lifetime (allocator), storage, name index.
         */
        struct TypedSlot {
            SlotAllocator                   allocator;
            std::unique_ptr<ISparseSet>     storage;
            /**
             * @brief name -> storage index.
             *
             * O(1) findByName backing. Populated from Resource::name on add(), erased
             * on remove().
             */
            std::unordered_map<std::string, uint32_t> nameIndex;
        };

        template<typename T>
        TypedSlot& getSlot() {
            TypeId id = typeId<T>();
            if (id >= m_slots.size()) m_slots.resize(id + 1);
            if (!m_slots[id]) {
                m_slots[id] = std::make_unique<TypedSlot>();
                m_slots[id]->storage = std::make_unique<SparseSet<T>>();
            }
            return *m_slots[id];
        }

        /**
         * @brief Look up the const slot for @p T, or nullptr if the type was never registered.
         *
         * The graceful lookup primitive behind isAlive / findByName /
         * forEachOfType (which return empty for an unknown type).
         */
        template<typename T>
        const TypedSlot* trySlot() const {
            TypeId id = typeId<T>();
            if (id >= m_slots.size() || !m_slots[id]) return nullptr;
            return m_slots[id].get();
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
         * @brief Erase the name->index mapping for @p name when it still points at @p index.
         *
         * Shared by remove() and rename() so the index stays consistent in one
         * place.
         */
        static void dropNameIndex(TypedSlot& slot, const std::string& name, uint32_t index) {
            if (name.empty()) return;
            auto it = slot.nameIndex.find(name);
            if (it != slot.nameIndex.end() && it->second == index) {
                slot.nameIndex.erase(it);
            }
        }

        /**
         * @brief Make @p name non-empty and unique among the names already registered in @p slot.
         *
         * Empty -> "asset"; a taken name gets the lowest free " (N)" suffix.
         * Called by add() so every asset has a usable key.
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

    private:
        std::vector<std::unique_ptr<TypedSlot>> m_slots;

        // Starts at 1 so a cache can hold 0 as "never synced" and repopulate on
        // its first pass without a special case.
        uint64_t m_epoch = 1;

        // Process-wide on purpose: the staging manager a scene load fills is a
        // second ResourceManager, and its assets have to be distinguishable from
        // the live ones they are about to replace. A per-manager counter would
        // hand both graphs the same ids.
        inline static std::atomic<uint64_t> s_nextUid{0};
};

} // namespace Engine
