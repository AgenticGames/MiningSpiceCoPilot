// CrossRegionProcessor.cpp - System 25: SVO+SDF Hybrid Volume Representation System
// Handles continuity of geological features across region boundaries

#include "25_SvoSdfVolume/Public/CrossRegionProcessor.h"
#include "SVOSystem/SVOHybridVolume.h"
#include "SVOSystem/MaterialSDFManager.h"
#include "SVOSystem/ZOrderCurve.h"

// Core system dependencies
#include "CoreServiceLocator.h"
#include "MemoryPoolManager.h"
#include "TaskScheduler.h"
#include "TransactionManager.h"
#include "EventBus.h"
#include "DependencyManager.h"
#include "RegionManager.h"

// Network-related includes
#include "Net/UnrealNetwork.h"

// Atomic operations and thread safety
#include "HAL/ThreadSafeBool.h"
#include "Templates/Atomic.h"

FCrossRegionProcessor::FCrossRegionProcessor()
    : bIsProcessingBoundary(false)
    , CurrentBoundaryOperation(EBoundaryOperationType::None)
    , CurrentRegionCount(0)
{
    // Service resolution through System 6
    ServiceLocator = IServiceLocator::Get().ResolveService<IServiceLocator>();
    check(ServiceLocator);
    
    RegionManager = ServiceLocator->ResolveService<IRegionManager>();
    check(RegionManager);
    
    MemoryManager = ServiceLocator->ResolveService<IMemoryManager>();
    check(MemoryManager);
    
    TaskScheduler = ServiceLocator->ResolveService<ITaskScheduler>();
    check(TaskScheduler);
    
    TransactionManager = ServiceLocator->ResolveService<ITransactionManager>();
    check(TransactionManager);
    
    EventBus = ServiceLocator->ResolveService<UEventBus>();
    check(EventBus);
    
    // Register for region-related events
    EventBus->SubscribeToEvent<FRegionBoundaryEvent>(this, &FCrossRegionProcessor::OnRegionBoundaryEvent);
    
    // Register this service
    IServiceLocator::Get().RegisterService<ICrossRegionProcessor>(this);
    
    // Register service dependencies
    FDependencyManager::Get().RegisterDependency<ICrossRegionProcessor, IRegionManager>();
    
    // Create shared memory buffer for boundary operations
    BoundarySharedBuffer = MemoryManager->CreateBuffer(
        "BoundarySharedBuffer",
        CrossRegionBufferSize,
        true, // Zero-copy
        true  // GPU writable
    );
    
    // Service health monitoring integration
    IServiceMonitor::Get().RegisterServiceForMonitoring("CrossRegionProcessor", this);
}

FCrossRegionProcessor::~FCrossRegionProcessor()
{
    if (EventBus)
    {
        EventBus->UnsubscribeFromAllEvents(this);
    }
    
    if (BoundarySharedBuffer)
    {
        MemoryManager->ReleaseBuffer(BoundarySharedBuffer);
        BoundarySharedBuffer = nullptr;
    }
    
    IServiceLocator::Get().UnregisterService<ICrossRegionProcessor>();
}

bool FCrossRegionProcessor::BeginCrossRegionOperation(TArray<FRegionID> RegionIDs, EBoundaryOperationType OperationType)
{
    FScopeLock Lock(&RegionMutex);
    
    if (bIsProcessingBoundary)
    {
        UE_LOG(LogCrossRegionProcessor, Warning, TEXT("Cannot begin cross-region operation - another operation is in progress"));
        return false;
    }
    
    if (RegionIDs.Num() < 2)
    {
        UE_LOG(LogCrossRegionProcessor, Warning, TEXT("Cross-region operation requires at least two regions"));
        return false;
    }

    bool bAllRegionsAvailable = true;
    
    // Check authority for network operations
    for (const FRegionID& RegionID : RegionIDs)
    {
        if (!RegionManager->HasRegionAuthority(RegionID))
        {
            UE_LOG(LogCrossRegionProcessor, Warning, TEXT("Missing authority for region %s"), *RegionID.ToString());
            bAllRegionsAvailable = false;
            break;
        }
        
        // Check if region is available for processing
        if (!RegionManager->IsRegionAvailableForProcessing(RegionID))
        {
            UE_LOG(LogCrossRegionProcessor, Warning, TEXT("Region %s is not available for processing"), *RegionID.ToString());
            bAllRegionsAvailable = false;
            break;
        }
    }
    
    if (!bAllRegionsAvailable)
    {
        return false;
    }
    
    // Begin transaction for all involved regions
    CurrentTransactionID = TransactionManager->BeginTransaction(ETransactionConcurrency::Optimistic);
    
    // Lock all regions for cross-region operation
    for (const FRegionID& RegionID : RegionIDs)
    {
        RegionManager->LockRegionForProcessing(RegionID, CurrentTransactionID);
        FSVOHybridVolume* Volume = RegionManager->GetRegionVolume(RegionID);
        InvolvedVolumes.Add(RegionID, Volume);
    }
    
    CurrentRegionIDs = RegionIDs;
    CurrentBoundaryOperation = OperationType;
    CurrentRegionCount = RegionIDs.Num();
    bIsProcessingBoundary = true;
    
    // Notify event system about cross-region operation start
    FEventContext Context;
    Context.Add("OperationType", (int32)OperationType);
    Context.Add("RegionCount", CurrentRegionCount);
    Context.Add("TransactionID", CurrentTransactionID);
    
    EventBus->PublishEvent<FCrossRegionOperationStartedEvent>(Context);
    
    UE_LOG(LogCrossRegionProcessor, Log, TEXT("Started cross-region operation of type %d involving %d regions"),
        (int32)OperationType, CurrentRegionCount);
    
    return true;
}

// Additional implementation omitted for brevity...