#include "MiningSpiceCoPilot.h"
#include "Modules/ModuleManager.h"
#include "13_GPUComputeDispatcher/GPUComputeDispatcherModule.h"

class FMiningSpiceCoPilotGameModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
        
        // Initialize GPU Compute Dispatcher submodule
        FGPUComputeDispatcherModule GPUModule;
        GPUModule.StartupModule();
    }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMiningSpiceCoPilotGameModule, MiningSpiceCoPilot, "MiningSpiceCoPilot");