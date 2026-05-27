#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

class RenderPass;
class ResourceManager;

/**
 * @brief Decouples pass instantiation from main.cpp.
 *
 * Each backend registers a builder per pass class name; main.cpp (or a
 * pipeline JSON loader, future work) walks a list of names and asks the
 * factory to construct each pass. This way adding a new pass touches the
 * backend's registration file only - the application's pass-list code
 * stays generic.
 *
 * Lookup is by string name and matches RenderPass::getName() so the
 * editor's pass introspection (Pipeline tab, Render Graph visualizer)
 * shows the same identifier the factory uses.
 *
 * Threading: registration runs once at backend init, before any frame.
 * No mutex - if a backend registered passes from worker threads we'd
 * add one, but that scenario isn't on the roadmap.
 */
class RenderPassFactory {
    public:
        RenderPassFactory(const RenderPassFactory& other) = delete;
        RenderPassFactory& operator=(const RenderPassFactory& other) = delete;

        RenderPassFactory(RenderPassFactory && other) = delete;
        RenderPassFactory& operator=(RenderPassFactory && other) = delete;

    public:
        using Builder = std::function<std::unique_ptr<RenderPass>(ResourceManager&)>;

        static RenderPassFactory& get();

        /// Register a builder for @p name. Replaces an existing builder
        /// when the same name is registered twice (e.g. plugin re-load).
        void registerPass(std::string name, Builder builder);

        /// Construct a pass by name. Returns nullptr if @p name is not
        /// registered or the builder itself returned nullptr.
        std::unique_ptr<RenderPass> create(const std::string& name,
                                           ResourceManager& resources) const;

        /// Snapshot the registered names. For editor / pipeline-config UI.
        std::vector<std::string> registeredNames() const;

        /// True when @p name has a registered builder.
        bool has(const std::string& name) const;

        /// Drop all registrations. Test-only / plugin-reload helper.
        void clear();

    private:
        RenderPassFactory() = default;

        std::unordered_map<std::string, Builder> m_builders;
};

} // namespace Engine
