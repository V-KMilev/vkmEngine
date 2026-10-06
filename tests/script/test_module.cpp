// The smallest gameplay module for the test suite: it stamps the engine version,
// registers one behavior, builds a scene and declares an event type of its own.
//
// The event type is the point: its bus's vtable and destructor are module code in
// a registry that outlives the module, so a reload that does not drop it makes the
// next flush call unmapped memory. Only a real shared library can test that case.
// The sound its scene carries is the same case in the asset graph.
#include "script/test_module.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "resource/asset/audio_clip_asset.h"
#include "resource/resource_manager.h"
#include "system/script/behavior.h"
#include "system/script/behavior_registry.h"
#include "system/script/reflected_behavior.h"

#include "system/script/module_entry.h"

namespace TestModule {

using namespace Vkm::Engine;

/// Declared here, so its Bus is this module's code and dies with it.
struct ModuleEvent {
    int value = 0;
};

class Ticker : public ReflectedBehavior<Ticker> {
    public:
        void onStart() override {
            subscribe([this](const ModuleEvent& e) { seen += e.value; });
        }

    public:
        int seen = 0;
};

/**
 * @brief A static of the kind GCC gives a unique symbol, which glibc binds across
 *        every copy of a library unless the module is built without them.
 */
struct LoadCount {
    static inline int value = 0;
};

} // namespace TestModule

VKM_REFLECT_BEGIN(::TestModule::Ticker)
    VKM_F(seen)
VKM_REFLECT_END()

VKM_MODULE_ENTRY
const char* vkmModuleEngineVersion() { return VKM_ENGINE_VERSION; }

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehavior<TestModule::Ticker>();
}

VKM_MODULE_ENTRY
void vkmBuildScene(Vkm::Engine::Scene& scene, Vkm::Engine::ResourceManager& resources) {
    using namespace Vkm::Engine;
    const EntityId id = scene.createEntity();
    scene.add(id, makeName("Built By Module"));
    scene.add(id, Transform{});

    // Made as a game makes a sound in code. The graph frees it after this module is
    // unmapped: safe only because ClipSamples built the control block in the engine.
    AudioClipAsset tone;
    tone.sampleRate = 8000;
    tone.channels   = 1;
    tone.samples    = ClipSamples(std::vector<int16_t>(80, 0));
    resources.add(std::move(tone), "module:tone");
}

VKM_MODULE_ENTRY
int vkmTestCountLoad() { return ++TestModule::LoadCount::value; }
