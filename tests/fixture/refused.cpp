// The modules a host refuses, or plays only offline, each built from this file by what it defines:
// VKM_FIXTURE_VERSION, the engine it says it was built for, if any; VKM_FIXTURE_REGISTERS, whether
// it has the entry that registers its behaviors. None has vkmSetupNetwork.

#include "system/script/module/module_entry.h"

#if defined(VKM_FIXTURE_VERSION)
VKM_MODULE_ENTRY const char* vkmModuleEngineVersion() { return VKM_FIXTURE_VERSION; }
#endif

#if defined(VKM_FIXTURE_REGISTERS)
VKM_MODULE_ENTRY void vkmRegisterBehaviors() {}
#endif
