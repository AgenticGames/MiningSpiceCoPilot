#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * GPU Compute Dispatcher module interface
 * Provides initialization and shutdown for the GPU compute dispatcher system
 */
class FGPUDispatcherModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Singleton getter */
    static FGPUDispatcherModule& Get()
    {
        return FModuleManager::LoadModuleChecked<FGPUDispatcherModule>("GPUDispatcher");
    }
    
private:
    /** Reference to the GPU dispatcher instance */
    TSharedPtr<class FGPUDispatcher> GpuDispatcher;
};