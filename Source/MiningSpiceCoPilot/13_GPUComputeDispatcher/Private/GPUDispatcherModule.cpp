#include "13_GPUComputeDispatcher/Public/GPUDispatcherModule.h"
#include "13_GPUComputeDispatcher/Public/GPUDispatcherLogging.h"
#include "13_GPUComputeDispatcher/Public/GPUDispatcher.h"
#include "6_ServiceRegistryandDependency/Public/ServiceLocator.h"

DEFINE_LOG_CATEGORY(LogGPUDispatcher);

void FGPUDispatcherModule::StartupModule()
{
    // Log module startup
    UE_LOG(LogGPUDispatcher, Log, TEXT("GPU Compute Dispatcher module starting up"));
    
    // Create and initialize the dispatcher
    TSharedPtr<FGPUDispatcher> Dispatcher = MakeShared<FGPUDispatcher>();
    if (Dispatcher->Initialize())
    {
        // Register with service locator
        Dispatcher->RegisterWithServiceLocator();
        
        // Keep a reference to prevent garbage collection
        GpuDispatcher = Dispatcher;
        
        UE_LOG(LogGPUDispatcher, Log, TEXT("GPU Compute Dispatcher initialized successfully"));
    }
    else
    {
        UE_LOG(LogGPUDispatcher, Error, TEXT("Failed to initialize GPU Compute Dispatcher"));
    }
}

void FGPUDispatcherModule::ShutdownModule()
{
    // Shut down the dispatcher
    if (GpuDispatcher.IsValid())
    {
        GpuDispatcher->Shutdown();
        GpuDispatcher.Reset();
    }
    
    UE_LOG(LogGPUDispatcher, Log, TEXT("GPU Compute Dispatcher module shut down"));
}

// This function is used to register the module with the engine
IMPLEMENT_MODULE(FGPUDispatcherModule, GPUDispatcher);