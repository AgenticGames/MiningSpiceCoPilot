#include "GPUComputeDispatcherModule.h"
#include "Public/GPUDispatcherLogging.h"
#include "Public/GPUDispatcher.h"
#include "../6_ServiceRegistryandDependency/Public/ServiceLocator.h"

// This submodule registration approach ensures proper loading of the GPU dispatcher
// without requiring it to be a separate UE module

void FGPUComputeDispatcherModule::StartupModule()
{
    UE_LOG(LogGPUDispatcher, Log, TEXT("GPU Compute Dispatcher starting up as submodule"));
    
    // Create and register dispatcher
    auto Dispatcher = MakeShared<FGPUDispatcher>();
    if (Dispatcher->Initialize())
    {
        Dispatcher->RegisterWithServiceLocator();
        UE_LOG(LogGPUDispatcher, Log, TEXT("GPU Compute Dispatcher initialized successfully"));
    }
    else
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Failed to initialize GPU Compute Dispatcher"));
    }
}

void FGPUComputeDispatcherModule::ShutdownModule()
{
    UE_LOG(LogGPUDispatcher, Log, TEXT("GPU Compute Dispatcher submodule shutting down"));
    
    // The service locator will handle shutdown of registered services
}