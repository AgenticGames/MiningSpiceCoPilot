#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * GPU Compute Dispatcher module interface - registers as a submodule of MiningSpiceCoPilot
 * This allows it to be loaded as part of the main module while maintaining its own initialization logic
 */
class FGPUComputeDispatcherModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    
    /** Access GPU dispatcher module */
    static FGPUComputeDispatcherModule& Get()
    {
        static const FName ModuleName = "MiningSpiceCoPilot";
        return FModuleManager::GetModuleChecked<FGPUComputeDispatcherModule>(ModuleName);
    }
};